# Imported N-API browser pnpm terminal loss

| | | Remarks |
| --- | --- | --- |
| **Status** | ▶️ | Active: the clean main-based port must restore the provider-level Buffer ownership boundary without Edge-specific copy hooks. |
| **Severity** | High | After the failure, subsequent shell commands such as `ls` produce no terminal output. |

## Failure

In `wasmer-sh`, enter the bundled Next.js example and run `pnpm install`.
The install appears to make progress, but afterward `pnpm run dev` and even
`ls` produce no output.

The smaller `node-express` install smoke completes and remains usable. The
Next.js reproduction emits repeated WISP messages for already-closed streams,
then Chrome reports an uncaught WebAssembly `RuntimeError: unreachable`. The
shell transcript subsequently becomes unavailable, indicating that the
browser runtime failed rather than Bash merely retaining the foreground
terminal.

The clean main-based port also exposes a deterministic provider regression:
Bash and `fs.readFileSync()` read the correct lockfile bytes, while
`fs.promises.readFile()` initially returns an equal-length zero-filled Buffer.
This is not a filesystem-binding bug. A browser `ArrayBuffer` cannot represent
an arbitrary subrange of WebAssembly linear memory, so pooled Buffers require a
mirror. The JS-backed N-API provider must synchronize that mirror whenever
control crosses between native guest code and observable host JavaScript.

## Current action plan

1. Keep mirror coherence and mirror lifetime as separate mechanisms in the
   JS-backed N-API provider.
2. Copy host bytes into live guest mirrors before native callback entry and
   after returning from host JavaScript.
3. Copy guest bytes into host values before invoking host JavaScript, settling
   a Promise, yielding to the host event loop, and returning from a native
   callback.
4. Release only mappings whose native callback frame has ended and which have
   no retaining N-API reference.
5. Remove backend-specific copy triggers from Edge; keep `lib/` and
   `src/internal_binding/binding_fs.cc` unchanged.
6. Verify focused async filesystem and gzip/network cases before the full
   browser Next.js gate.

## Root causes and fix

The imported Edge runtime replaces globals on the shared worker realm. Panic
and worker-error reporting dynamically called `console.error`, while thread
parallelism dynamically read `navigator`. After Edge started, both lookups
could recursively enter N-API while the guest was already active. The SDK now
captures the host console and hardware concurrency before starting Edge.

Large installs also exposed excess copying in the native N-API buffer bridge.
Buffer, array-buffer, typed-array, and data-view lookups retained their host
snapshot and could request a second snapshot before copying into guest memory.
They now consume and free the existing snapshot. Allocations of at least 16 MiB
are no longer rounded up to the next power of two.

Finally, the Node and browser WISP network bridges could report socket `close`
before buffered data had been drained. Close is now deferred until the guest
reads the remaining bytes. The WISP receive allowance is 128 MiB for large
package archives; the associated memory-exhaustion concern is recorded in
`SECURITY-HOST-JS-NAPI.md`.

All fixes are in Wasmer SDK or native N-API code. Edge files under `lib/` and
`src/internal_binding/binding_fs.cc` remain unchanged.

## Verification

The optimized SDK was rebuilt against the required local Wasmer checkout:

```sh
cd js
WASMER_REPO=/Users/syrusakbary/Development/wasmer npm run build
```

The artifact is 4.35 MiB raw / 1.11 MiB Brotli. The SDK test suite passes, as
does this focused browser sequence:

```sh
cd wasmer-sh
WASMER_TEST_PNPM_ONLY=1 WASMER_PNPM_TIMEOUT=120000 npm test
```

That test now requires, in order: successful `pnpm install`, a returned Bash
prompt, visible output from a following `ls`, and output from a following Node
process. The fresh Next fixture also completed install and passed its
post-install `ls` assertion.

Next's development server did not reach its ready marker during the final
bounded diagnostic run. At the point the test was stopped, the renderer used
about 4.6 GiB RSS. This is now a separate performance/runtime compatibility
issue rather than terminal loss.

## Original action plan

1. Change the focused Next browser regression to run install separately from
   the development server.
2. Require the Bash prompt to return after install and verify a subsequent
   `ls` marker before attempting `pnpm run dev`.
3. Preserve browser `pageerror`, worker, and terminal diagnostics so the first
   Wasm trap has an actionable stack instead of surfacing only as a silent
   terminal.
4. Determine whether the trap is in Edge's imported N-API callback path, the
   Wasmer JSPI/yield boundary, or WISP socket completion handling.
5. Fix the native N-API or Wasmer side; do not modify files under EdgeJS
   `lib/` or `src/internal_binding/binding_fs.cc`.
6. Rebuild the N-API-backed Edge WebC and Wasmer SDK as needed, then validate
   install, prompt recovery, `ls`, and Next development-server startup.

## Initial verification

Registry-backed `syrusakbary/edgejs@0.1.8`:

```sh
cd wasmer-sh
WASMER_TEST_PNPM_ONLY=1 WASMER_PNPM_TIMEOUT=240000 npm test
```

passes for `node-express`.

The existing combined Next command:

```sh
WASMER_NEXT_TIMEOUT=600000 node tests/next-browser.mjs
```

reproduces the WebAssembly `unreachable` and terminal loss before the Next
ready marker.
