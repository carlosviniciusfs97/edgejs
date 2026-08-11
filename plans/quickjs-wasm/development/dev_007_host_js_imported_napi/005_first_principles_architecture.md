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

## Implementation status (2026-08-10)

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
- [ ] Convert every remaining Edge raw-pointer consumer to the lease API,
  continuing with retained crypto/TLS state and the remaining BufferSource
  consumers in process, spawn, HTTP, module loading, messaging, ICU, UDP, and
  WebAssembly bindings. Classify guest-backed control blocks separately: their
  pointers are intentionally shared and must not be converted to copied
  leases.
- [ ] Move the remaining WASIX/libuv lifecycle issues to Wasmer and run the
  complete native/browser/wasmer-sh/pnpm/Next.js acceptance matrix.

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

The next implementation slice is the remaining raw-pointer audit. Each caller
must be classified as a synchronous non-reentrant access, an asynchronous or
reentrant retained access which needs a lease, or an intentionally shared
guest-backed control block. Conversion is driven by that lifetime, not by the
compile target. Engine string limits and lifecycle hooks then move behind
provider capabilities before the complete browser/wasmer-sh acceptance run.

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
