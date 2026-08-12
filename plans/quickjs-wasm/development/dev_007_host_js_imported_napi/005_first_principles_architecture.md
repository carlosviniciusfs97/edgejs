# First-Principles Host-JavaScript N-API Architecture

## Objective

Run Edge.js as a WASIX WebAssembly module which imports N-API and executes
JavaScript in the browser or Node.js host engine, without embedding QuickJS or
V8.

The native and WASIX builds must execute the same Edge logic. The only runtime
behavior which may differ between providers is the implementation of the host
event-loop checkpoint: an embedded provider performs a synchronous checkpoint,
while the host-JavaScript provider suspends through JSPI so the browser or
Node.js event loop can run.

No host-JavaScript behavior may be selected in Edge with `#ifdef __wasi__`.
Platform syscall differences belong below Edge in WASIX/libuv. JavaScript
engine and memory-topology differences belong behind N-API provider contracts.

Files under `lib/` must remain unchanged.

## Implementation status (2026-08-12)

- [x] Rebased the clean N-API architecture branch on current `main` while
  preserving the host-JavaScript backend on top.
- [x] Routed `napi_run_script` through the environment-owned virtual global
  instead of the Wasmer worker's real `globalThis`.
- [x] Added a two-environment isolation contract test and an SDK host-JS
  integration test.
- [x] Removed SDK captures of `console` and host hardware concurrency which
  existed to survive guest pollution of the worker global; Edge integration
  tests still pass without them.
- [x] Added a provider-neutral opaque buffer lease for V8, QuickJS, native
  Wasmer imports, and host-JavaScript Wasmer imports. The contract test closes
  the value's handle scope before releasing the lease and verifies exact-range
  copy-back.
- [x] Converted zlib retained input/output pointers to leases. Its release path
  no longer reconstructs a `napi_value`; sync continuation and async zlib tests
  pass with exact subranges on the host-JavaScript provider, and a native V8
  smoke test passes.
- [x] Made shared control-block typed-array creation a single provider-neutral
  call in `EdgeCreateSharedTypedArray`.
- [x] Audited the remaining Edge target checks by concern. Filesystem buffer
  access is the largest remaining provider leak; stream ingress and unsafe
  ArrayBuffer allocation expose missing provider-neutral ownership operations;
  process, DNS, pipe, HTTP/2, and OS checks are platform/libuv work and must be
  evaluated separately rather than mechanically removed.
- [x] Converted filesystem stats, scalar reads/writes, string writes, and
  vectored writes to leases. Async requests own lease arrays directly and
  release them on completion, cancellation, and teardown; no JavaScript value
  is reconstructed during release. Native and host-JavaScript tests cover sync
  and async exact ranges plus vectored writes.
- [x] Removed the transitional `acquire_buffer_access` API after the final Edge
  caller migrated. Environment teardown now discards outstanding leases and
  returns copied guest allocations through the same environment ownership
  boundary.
- [x] Reduced Edge scheduling to one provider-owned event-loop checkpoint.
  V8, QuickJS, native Wasmer imports, and the host-JavaScript JSPI provider now
  implement the same operation; Edge no longer selects microtask versus host
  yielding behavior by target.
- [x] Replaced `unofficial_napi_process_microtasks` and
  `unofficial_napi_yield_to_host_event_loop` with one provider checkpoint API.
  Its semantic mode distinguishes a microtask-only checkpoint from a host-task
  turn; this preserves Node ordering without encoding the provider or target.
  Embedded idle policy is implemented by the embedded provider rather than
  duplicated in the Rust adapter.
- [x] Made provider-owned asynchronous work part of the checkpoint result.
  Dynamic-import promises are retained and observed inside the V8, QuickJS,
  and host-JavaScript providers; Edge no longer owns a second dynamic-import
  liveness registry.
- [x] Moved unsafe ArrayBuffer allocation policy behind N-API. Edge no longer
  chooses between an external native allocation and a host-engine allocation.
- [x] Made stream ingress provider-neutral. Default reads transfer ownership
  with standard N-API on every provider, while user-supplied read buffers keep
  one explicit read/write lease for the complete libuv retention interval and
  publish bytes before JavaScript reentry.
- [x] Added a common scoped lease for synchronous Edge byte consumers and
  migrated TextEncoder/TextDecoder access to it. Writable encoding output is
  published before updating JavaScript-visible result state. V8, QuickJS, and
  host-JavaScript leases accept exact SharedArrayBuffer ranges through the same
  contract; provider tests cover read/write access and copy-back.
- [x] Replaced SDK shutdown's one-timer-turn cleanup guess with an explicit
  scheduler drain. Acknowledged shutdown waits until every active WASIX task
  reaches its worker-idle boundary before worker handles are dropped; ordinary
  drop remains immediate abandonment. Repeated full host-JavaScript suites now
  release consecutive clients without racing guest cleanup.
- [x] Removed the SDK's decoding and mutation of libc-private pthread control
  blocks. Targeted WASIX cancellation now retains only the semantic
  `(pid, tid)` to browser-worker ownership map; normal shutdown never simulates
  guest thread-exit bookkeeping.
- [x] Migrated synchronous one-shot crypto byte access to the common lease
  contract: hash/HMAC, PBKDF2, scrypt, the non-AEAD cipher transform, random
  bytes, and exact-range in-place random fill. The host-JavaScript integration
  test uses host-owned subviews and verifies that writable copy-back cannot
  modify bytes outside the requested range.
- [x] Reduced `spawnSync` stdin parsing to one read lease and an owned native
  copy instead of reconstructing raw pointers for each BufferSource shape.
  Native Edge verifies an exact DataView subrange. Browser execution currently
  stops later at the WASIX process layer with `spawnSync /bin/node EINVAL`, so
  that runtime capability remains a Wasmer acceptance-matrix item rather than
  an Edge memory workaround.
- [x] Migrated ICU streaming decode and `buffer.transcode` input to scoped read
  leases. The lease ends immediately after ICU consumes the bytes, before the
  JavaScript result is created; native and host-JavaScript tests cover exact
  UTF-8 subviews and UTF-16LE transcoding.
- [x] Converted every remaining Edge raw-pointer byte consumer to the lease
  API, including retained stream/libuv writes, crypto/TLS, process and V8
  output buffers, spawn, HTTP, module loading, messaging, ICU, UDP, and
  WebAssembly bindings. Guest-backed control blocks are classified separately
  below because their pointers are intentionally shared and must not be
  converted to copied leases.
- [x] Removed the Edge-side consequences of the old provider leaks: zlib no
  longer reconstructs JavaScript values when releasing retained bytes, workers
  no longer force a first-message keepalive, pipes no longer downgrade IPC on
  WASI, and module evaluation no longer tracks dynamic-import promises in
  Edge. The `lib/` tree remains unchanged.
- [x] Removed SDK host timer/Promise captures, the scheduler sleep-message
  protocol, and the permanent wake worker. Non-zero WASIX sleeps use the
  generic worker-scoped timer design from SDK main; zero-duration nonblocking
  polls complete immediately and Edge performs its explicit provider
  checkpoint at the event-loop boundary.
- [x] Rebuilt Edge with exnref, validated the engine-free import surface (110
  N-API imports and 91 extension imports), rebuilt the SDK against
  `/Users/syrusakbary/Development/wasmer`, and passed all 18 focused host-JS
  Edge integration tests plus the SDK runtime, multi-worker, and isolated
  host-N-API tests.
- [x] Ran the complete WASIX Node compatibility selection with four workers:
  1,671 tests passed and five HTTP/2/TLS tests failed under parallel load; all
  five passed immediately when rerun alone. Treat these as harness contention,
  not product defects, unless a serial run reproduces one.
- [x] Corrected the guest architecture identity at its source: a wasm32 Edge
  build now reports `process.arch === "wasm32"` and `os.arch() === "wasm32"`
  instead of the impossible value `"unknown"`. This is platform description,
  not a WASI-specific execution path.
- [x] Traced repeated-install downloads to pnpm 10 itself. The exact 10.34.5
  bundle downloaded a skipped Windows optional package on the second install
  under native macOS and grew a fresh store by about 100 MB. pnpm 10 already
  provides `optimisticRepeatInstall` as its supported fast no-op path, so the
  Edge package enables that option rather than changing Edge, N-API, Wasmer,
  or pnpm internals. A fresh browser reproducer completed its first install in
  788 ms and its unchanged second install in 288 ms with no incompatible
  download.
- [x] Completed the browser/wasmer-sh/pnpm/Next.js acceptance matrix against a
  fresh Vite worker graph: `edge --version` and `node --version` both report
  `v24.13.2-pre`; timers return to the prompt; the Next fixture installs in
  15.4 seconds, repeats in 315 ms, and leaves later `ls` output intact; and
  `pnpm run dev` reaches ready in 680 ms, serves `/` with HTTP 200, and renders
  the in-browser preview.
- [ ] Implement the advertised `node:sqlite` builtin as a separate Node
  compatibility feature. A pnpm 11 experiment correctly exposed that Edge's
  catalog currently admits the builtin while its public loader receives no
  exports. Do not fake the module or tie that gap to the host-JavaScript N-API
  provider.
- [x] Fixed the native indirect-`eval()` dynamic-import rejection at the N-API
  provider's bytecode metadata boundary. Function-shaped bytecode no longer
  invents the CJS default-loader symbol when the caller supplied no host ID,
  and the dynamic-import callback falls back to the realm-global ID only when
  the host-options block is actually absent, matching Node. Internal builtins
  therefore retain an absent referrer while the CJS loader continues to pass
  its default-loader symbol explicitly. Successful entry execution now closes
  through Edge's existing provider-neutral callback checkpoint, matching
  Node's microtasks-before-ticks rule; no provider-specific scheduling branch
  or promise registry was needed. Provider and upstream Node regressions cover
  both the absent-ID rejection and explicit default-loader success paths.
- [x] Made the event-loop checkpoint result say whether the provider actually
  admitted a host-task turn. Host-JavaScript reports that admission after its
  JSPI suspension; embedded V8 and QuickJS do not manufacture progress with a
  one-millisecond sleep. When neither JavaScript nor the provider has immediate
  work, Edge now waits on libuv's native event source with `UV_RUN_ONCE`. This
  removes the polling latency behind the HTTP header-limit regression without
  adding a target or provider branch to Edge. Acceptance passed with 69/69 V8
  and 71/71 QuickJS N-API tests; 1,749/1,749 V8 and 1,741/1,741 QuickJS native
  Edge tests; 1,676/1,676 V8 and 1,675/1,675 QuickJS serial WASIX tests; the
  wasmer-sh browser smoke; and the browser Next.js install/dev/preview flow.

## Architectural boundaries

There are three independent concerns. They must not be represented by one
target check.

| Concern | Owner |
| --- | --- |
| Host JavaScript scheduling and JSPI suspension | N-API provider and Wasmer async host-function API |
| JavaScript backing stores versus guest linear memory | Provider-neutral N-API memory API |
| WASIX syscall and libuv feature support | Wasmer/WASIX/libuv |

Edge owns Node-compatible behavior. It must not compensate for missing host
scheduler notifications, non-coherent provider buffers, or missing WASIX
syscalls.

## Required invariants

### Environment isolation

Each N-API environment owns a JavaScript global context. All script,
CommonJS-function, module, `vm`, and dynamic-import evaluation must use that
context. Guest Node bootstrap code must never replace or mutate the Wasmer SDK
worker's actual `globalThis`, timers, `Promise`, `console`, `navigator`, worker
bridge, or WebAssembly objects.

The host primitives used by wasm-bindgen, JSPI, and the SDK scheduler belong to
the control plane. Capturing them before guest startup may be a defensive
measure, but must not be required for correctness.

### Buffer ownership

An arbitrary host `ArrayBuffer` cannot alias a subrange of guest WebAssembly
linear memory. Therefore a transient copy cannot truthfully implement the
standard N-API raw-pointer lifetime in every case.

Edge native code which needs bytes must use a provider-neutral opaque memory
lease:

```cpp
acquire_memory(env, value, offset, length, mode, &lease, &pointer);
release_memory(env, lease, modified);
```

The lease owns the JavaScript value, any guest snapshot, copy-in/copy-out, and
pointer lifetime. Embedded providers implement the same operation with a
direct pointer. Edge never branches by target or provider.

Native code and JavaScript sometimes require a genuinely shared control block,
such as `tickInfo`, timer state, stream state, or HTTP/2 state. A second
provider-neutral operation creates a typed-array view backed by guest memory:

```cpp
create_guest_backed_typedarray(env, type, length, &pointer, &value);
```

Standard N-API pointer emulation may remain as a compatibility fallback for
third-party modules, but Edge hot paths must have explicit ownership and must
not globally mirror every buffer around every N-API call.

### Callback reentry

Every bridge operation which can synchronously execute JavaScript installs a
synchronous callback context tied to its current `FunctionEnvMut`. Callbacks
which arrive after JSPI suspension use a persistent weak async-store handle and
nonblocking store acquisition. They are deferred if the store is not available.

Correct callback-context ownership must make unsafe reentry through the current
Wasmer store unnecessary.

### Scheduling

Edge performs one logical host checkpoint per event-loop turn. The provider
owns its implementation:

- Embedded/native: drain the engine checkpoint and wait as appropriate.
- Host JavaScript: suspend through JSPI, allow host tasks and promises to run,
  drain deferred native callbacks, and finish the checkpoint.

Edge must not expose separate provider-specific `process_microtasks` and
`yield_to_host_event_loop` branches. Pending host work must be represented by
the provider checkpoint rather than Edge-specific dynamic-import or worker
keepalive state.

### Worker values and messages

Cross-worker JavaScript values are transported by `postMessage`, whose native
structured-clone implementation is the serializer. The SDK may retain a
central value until the destination obtains it and must release all origin and
destination copies explicitly. It must not implement a second JavaScript value
serializer.

### Platform features

Missing pipe handle passing, thread termination, futex wakeup, or other WASIX
behavior is fixed in Wasmer/WASIX/libuv. Edge must not downgrade libuv modes or
edit libc pthread control blocks.

## Repository responsibilities

### Edge.js

Keep the package manifest, command aliases, certificate and pnpm mounts, exnref
build, import validator, tests, and deferred security notes. Use the common
memory-lease, shared-control-block, and host-checkpoint APIs everywhere.

Remove newly introduced target/provider branches, zlib reference reconstruction,
dynamic-import promise tracking, worker first-message keepalive, and the WASIX
pipe downgrade. Generic fixes which are valid for every provider should be
landed on main independently.

### wasmer-napi

Keep the host-JavaScript backend, per-environment global context, runtime hook,
JSPI checkpoint, callback trampoline, explicit memory operations, and
structured-clone worker bridge.

Share common guest ABI decoding with the native backend. Isolate the
conservative standard-pointer compatibility layer from explicit Edge memory
leases. Do not publish and refresh all mirrored buffers around unrelated N-API
calls.

### Wasmer SDK

Keep N-API runtime-hook integration and generic cross-worker C-API value
routing. Remove Edge-specific timer, wake-worker, global-capture, and pthread
layout workarounds after their underlying N-API or Wasmer defects are fixed.
Local Wasmer path overrides are development configuration and must not be
published.

### Wasmer

Provide safe access to the JavaScript memory backing buffer, weak async-store
handles, and nonblocking reacquisition after suspension. Expose supported
WASIX thread termination and shared-memory operations rather than raw internal
layouts or VM handles. Fix pipe IPC semantics at the libuv/WASIX layer.

## Execution order

1. Rebase Edge and wasmer-napi on their current `main`; keep Wasmer based on
   `sdk` and preserve main behavior on overlapping changes.
2. Route every host-JavaScript evaluation path through its environment context.
3. Add environment-isolation tests which compare host-global property identity
   before and after Node bootstrap and normal script execution.
4. Introduce opaque memory leases and implement them for native and host-JS
   providers.
5. Convert all Edge pointer consumers and shared control blocks to the common
   APIs, not only call sites exposed by current tests.
6. Reduce scheduling to one provider-owned host checkpoint.
7. Remove Edge dynamic-import, worker-keepalive, pipe, zlib-reference, and
   buffer conditionals.
8. Remove SDK host-global captures, scheduler timers, dedicated wake worker,
   and pthread-memory manipulation as their root causes disappear.
9. Move remaining syscall and lifecycle defects to Wasmer/WASIX.
10. Rebuild and verify native, Node, browser, wasmer-sh, pnpm, workers,
    networking, dynamic imports, and Next.js.

Steps 1 through 8 are now implemented on the clean branches. Step 9 remains a
layering rule for defects discovered by acceptance testing. Step 10 is partly
complete: native focused tests, the full WASIX Node selection, SDK worker and
runtime tests, and the host-JavaScript integration matrix have run; browser
wasmer-sh, pnpm install/dev, and a Next.js development server remain the next
end-to-end gates.

## Current conditional audit

The audit distinguishes what a branch observes from which layer must own the
difference. A target macro is not an acceptable substitute for either a
provider capability or an operating-system capability.

| Edge area | Observed difference | Root owner and action |
| --- | --- | --- |
| `binding_fs.cc` | Host buffers do not always alias guest memory | Convert sync and async I/O to opaque leases; the async request owns leases directly and releases them on every completion/cancellation path. Remove buffer references that exist only to reconstruct values at release. |
| `edge_stream_base.cc` | A guest allocation cannot be adopted as a host `ArrayBuffer` | Add/use a provider operation that creates an owned JavaScript byte value from a guest range. Stream logic asks for that operation without knowing whether the provider adopts or copies. |
| `edge_buffer.cc` unsafe allocation | Embedded V8 can adopt `malloc`; a host engine cannot adopt a guest pointer | Make allocation policy an N-API provider operation. Edge requests an uninitialized owner-backed ArrayBuffer and never chooses external versus engine-owned storage. |
| `edge_runtime.cc` | Host JavaScript must suspend so external tasks can run | This is the one intentional behavioral difference. Replace the two Edge branches with one provider-owned checkpoint API whose host implementation uses JSPI and whose embedded implementation drains/waits synchronously. |
| process, DNS, pipe, HTTP/2, OS wrappers | WASIX exposes different OS/libuv facilities | Keep true OS portability below Node logic where possible. Add focused libuv/WASIX contract tests before moving or removing each check; do not conflate these with the JavaScript provider. |
| engine string limits | V8 and QuickJS have different engine limits | Query a provider capability or enforce the Node-compatible public limit; do not infer the engine from the compile target. |
| environment embedder hooks | Embedded providers currently expose lifecycle hooks unavailable through imported N-API | Make lifecycle registration part of the provider contract, then call it unconditionally. Environment cleanup must release outstanding leases and callbacks. |

The raw-pointer audit is complete. Every BufferSource byte consumer now either
owns a scoped lease or transfers a lease into the asynchronous request that
retains the pointer. Raw `napi_get_*_info` calls outside the exceptions below
request metadata only.

The remaining direct pointers all originate from
`unofficial_napi_create_guest_backed_typedarray`; they are intentionally aliased
control blocks rather than copied BufferSource access:

| Control block | Native/JavaScript users | Why it remains shared |
| --- | --- | --- |
| Buffer flags and module-loader flags | buffer/module bindings and bootstrap JavaScript | JavaScript and native code observe the same small state word across calls. |
| Stream state, task queue, and timer state | stream callbacks, next-tick scheduling, immediates, and timers | Both sides update scheduling state without an acquire/release boundary. |
| Async-hook fields, async IDs, and ID stack | `binding_async_wrap.cc` and `edge_async_wrap.cc` | Async context transitions read and write the same state throughout nested callbacks. |
| HTTP/2 session and stream fields | native nghttp2 callbacks and JavaScript HTTP/2 state | Callback-driven state is synchronously shared in both directions. |

Conversion was driven by lifetime rather than the compile target. Engine
string limits and lifecycle hooks can now move behind provider capabilities
before the complete browser/wasmer-sh acceptance run.

## Acceptance gates

- No new `__wasi__` or embedded-provider checks in Edge runtime logic.
- Edge `lib/` is byte-for-byte unchanged.
- Host `globalThis`, timers, `Promise`, `console`, `navigator`, WebAssembly, and
  SDK bridge identities are unchanged after Edge startup and execution.
- Buffer tests cover offsets, pooled backing stores, asynchronous read/write,
  nested callbacks, retained pointers, memory growth, and cleanup.
- Zlib copies an input at most once per retained lease rather than once per
  output-sized continuation.
- Native Edge behavior and tests remain unchanged.
- `edge --version` and `node --version` work through wasmer-sh.
- pnpm install and post-install complete without polling-scale delays; later
  commands continue producing terminal output.
- Worker startup, termination, messages, dynamic imports, timers, promises,
  HTTP, and a Next.js development server pass in browser and Node hosts.
- The published artifact imports N-API, embeds no JavaScript engine, uses
  exnref, contains the expected certificates and package-manager assets, and
  is named `syrusakbary/edgejs`.

## Removal rule

No workaround is retained merely because deleting it re-exposes a failure.
When a removal fails, record the violated invariant, trace it to the owning
layer, add a focused contract test there, and fix that layer before continuing.
