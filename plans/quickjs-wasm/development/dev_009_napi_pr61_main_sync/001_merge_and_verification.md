# N-API PR #61 main sync and verification

| | | Remarks |
| --- | --- | --- |
| **Status** | ▶️ | Scoped direct reentry is implemented and propagated through the N-API, Wasmer SDK, and EdgeJS PRs; GitHub CI verification remains active. |
| **Scope** | N-API PR #61, Wasmer PR #6956, EdgeJS PR #149 | Guest heap store lending, callback reentry, SDK integration, resource budgets, host termination, and V8 worker configuration. |

## Dependencies

- PR branch: `fix/guest-heap-store-lending` at `6c3ea892` before this sync.
- Base branch: current `wasmerio/napi:main`.
- Wasmer APIs used by the PR: `StoreMut::parked`, `Store::with_current`, and
  the stacked Wasmer changes in PRs #6886, #6887, and #6888.
- Existing EdgeJS and nested QuickJS working-tree changes must remain untouched.

## Write ownership

- Perform all N-API merge and conflict edits in an isolated Git worktree.
- Push only the PR's own `fix/guest-heap-store-lending` branch.
- Update this task note from the EdgeJS root after the N-API branch is stable.
- The follow-up request explicitly authorizes advancing the EdgeJS N-API
  gitlink and SDK pin after the combined Wasmer branch is stable.

## Action plan

1. Fetch N-API `main` and the PR head, then create an isolated worktree at the
   PR head.
2. Merge `origin/main` into the PR branch without rewriting its published
   history.
3. Resolve conflicts by preserving the PR's store-lending and host-control
   invariants while adopting current main's standalone CLI, target-specific
   budgeting, manifest, and test changes.
4. Inspect the complete post-merge diff against `main` for accidental scope
   expansion and run formatting and whitespace checks.
5. Run the focused library tests introduced by the PR, followed by the native
   V8 and QuickJS CI-equivalent suites supported by the repository.
6. Push the resolved branch, monitor all PR checks, and address any failure
   until the PR is green and mergeable.
7. Reproduce the remaining V8 WASIX `process.report.getReport()` failure with a
   clean standalone runner, trace the exact nested callback boundary, and add a
   focused regression before advancing the N-API, Wasmer, and EdgeJS stack.

## Integration progress

- Merged current N-API `main` into `fix/guest-heap-store-lending` as
  `d6dd23a`, resolving five manifest/runtime conflicts without expanding the
  effective PR diff beyond the guest-heap and host-control work.
- Merged the complete Wasmer #6886 → #6887 → #6888 stack into
  `codex/js-napi-host-reentry-sdk` as `57f98264b27`, resolving the N-API
  submodule to the synced PR #61 head.
- Verified the combined Wasmer workspace with 4 store-lending API tests, 44
  N-API library tests, and 244 passing WASIX library tests (2 ignored).
- Pinned N-API standalone builds to immutable Wasmer commit `57f98264b27` and
  pushed N-API head `99a58dd`; the locked standalone library suite passes all
  44 tests.
- Advanced Wasmer SDK PR #6956 to N-API head `99a58dd` in `83cf85c3292`.
- The first EdgeJS V8 WASIX smoke exposed one missed reentry boundary:
  `unofficial_napi_contextify_run_script` ran JavaScript while its import still
  held the store. A scoped trace showed the callback trampoline reaching a
  store-context entry with one live borrow. N-API commit `4409705` now routes
  only that execution bridge through the existing `with_cb_context` guard.
- Advanced Wasmer SDK PR #6956 to the contextify fix at `7f091a44738` and the
  EdgeJS PR worktree to N-API head `4409705`.
- Rebuilt the complete V8 WASIX guest, validated its 110 N-API and 67 extension
  imports, rebuilt the locked release `napi_wasmer` runner, and passed the
  Makefile smoke with `hello world!`.
- Fixed the SDK stack's Rust 1.95 lint and NodeJS test-feature regressions in
  Wasmer commit `1e295461b57`. The exact NodeJS WASM test build and full
  `make lint-packages` target pass locally. The dependency cooldown advisory is
  intentionally out of scope because crates.io yanked the eligible
  `chacha20` releases while their replacement remains inside the repository's
  three-day quarantine.
- Pinned the N-API standalone manifest and lockfile to that latest SDK commit
  in N-API head `625777a`, then advanced EdgeJS PR #149 to that N-API head.
  The locked standalone N-API library suite still passes all 44 tests.
- Applied the exact Taplo formatting required by Wasmer CI in N-API commit
  `1e11604` and Wasmer SDK commit `5a02e54a540`, then advanced EdgeJS to the
  formatting-complete N-API head. These follow-up commits contain no runtime
  behavior changes.
- Fixed two Rust 1.95 C-API import lints in Wasmer functional head
  `c83ba6d315d`, pinned N-API standalone builds to it in `79a1822`, and
  advanced EdgeJS to that N-API head. Wasmer head `19011b95302` then updates
  only its N-API submodule pointer, avoiding an unresolvable circular latest-
  commit pin while EdgeJS still consumes every functional SDK change.
- The full V8 WASIX suite at EdgeJS `de75f0e9` still failed 74 tests after its
  successful build and import validation. The common primary error was
  `[callback trampoline] no store lent to this thread`; the later
  `TypeError: fn is not a function` and missing test-reporter module errors
  were downstream bootstrap/module-loading failures.
- Compared the guest bridges and found that `src/guest/napi_js.rs` already
  lent the store while evaluating `module_wrap`, but the V8 bridge in
  `src/guest/napi.rs` did not. N-API commit `4f1b39b` now applies the existing
  `with_cb_context` guard to synchronous and asynchronous module evaluation.
  This is deliberately limited to the JS-executing boundary and does not
  change evaluation semantics.
- Audited the remaining WASIX guest-to-host imports for store-lending needs.
  The strict boundary is a host operation that can synchronously invoke guest
  code (or explicit teardown that drains guest finalizers), not every helper
  that happens to allocate or inspect a JavaScript value. Broad speculative
  guards around typed arrays, buffers, errors, and serialization were rejected
  to keep the normal N-API hot path unchanged.
- N-API commit `d781939` adds only the two confirmed missing boundaries:
  required-module-facade construction, which internally evaluates its source
  module, and explicit environment release, which drains guest finalizers. The
  teardown path unregisters the environment from scheduling first, retains its
  callback context and heap accounting during finalizer dispatch, and reclaims
  that state afterward.
- Added focused coverage to the existing `run_script_test`: a synthetic module
  verifies that required-module-facade construction invokes its guest evaluation
  callback exactly once, while the existing explicit-release assertions verify
  that wrap and add-finalizer callbacks are each dispatched exactly once. The
  combined test passes both natively and through the standalone WASIX runner
  with `RUN_SCRIPT_TEST_OK=1`.
- Advanced Wasmer SDK PR #6956 to the focused N-API head in commit
  `65b33bf134c`, then advanced EdgeJS to the same N-API revision. A clean locked
  `napi_wasmer` build, full V8 WASIX rebuild/import validation, CLI smoke, and
  representative buffer-constructor, diagnostics-channel module-import, and
  HTTP proxy-fetch tests all pass locally.
- EdgeJS CI then exposed one remaining callback-reentry boundary in
  `test-http-agent-reuse-drained-socket-only`: `process.report.getReport()`
  enumerates the `process.env` Proxy while the report callback owns the WASIX
  store. `napi_get_property_names` entered V8 without lending that store, so
  the Proxy's guest `ownKeys` trap could not re-enter. Its failed trampoline
  returned `undefined`, producing the downstream V8
  `CreateListFromArrayLike called on non-object` error. The TCP connection and
  agent-reuse behavior were not the cause.
- A scoped trace confirmed that the outer guest callback and nested `ownKeys`
  callback ran on the same thread, with the store borrow held specifically
  across property-name enumeration. The fix is intentionally limited to
  `napi_get_property_names` and `napi_get_all_property_names`, the two N-API
  operations that synchronously invoke a Proxy `ownKeys` trap. No broad
  lending was added to ordinary property access, allocation, or conversion
  paths.
- Added a deterministic standalone N-API regression in `run_script_test`: an
  outer guest callback invokes both enumeration APIs on a Proxy and verifies
  that each re-enters the guest `ownKeys` callback. A clean locked native build,
  the WASIX regression, and the `wasm32-unknown-unknown` JavaScript-backend
  feature check pass. The exact EdgeJS test also passes with the clean runner,
  followed by 100 sequential repetitions with zero failures.
- Pushed the focused enumeration fix to N-API PR #61 as `1add135` and advanced
  Wasmer SDK PR #6956 to that N-API revision as `ce7c0b598ba`.
- Replaced N-API's generic `StoreMut::parked` / `Store::with_current` callback
  path with a stack-scoped pointer to the active `FunctionEnvMut`. Callback and
  finalizer trampolines validate both the owning thread and the current TLS
  invocation token before dereferencing it. The token is cleared while guest
  code executes, so an unguarded nested import cannot alias the store already
  borrowed by `Function::call`; properly guarded nested imports install and
  restore their own token.
- Split guest-heap allocation into a store-free arena fast path and an explicit
  `alloc_with_store` growth path. Ordinary import handlers pass their existing
  store directly. A foreground V8 allocator call can use the scoped invocation
  token, while a background V8 allocator thread has empty TLS and remains
  arena-only. N-API no longer contains any `Store::with_current` or `parked`
  call sites.
- Removed the callback guard from `napi_get_global` in both native and
  JavaScript guest bridges. The native bridge only obtains V8's context-global
  handle and stores it, while the JavaScript bridge performs the equivalent
  direct global lookup; neither runs guest JavaScript or allocates a guest
  backing store.
- Verified the final N-API implementation with Rust formatting and whitespace
  checks, the 44-test standalone library suite, the
  `wasm32-unknown-unknown` JavaScript-backend feature check, and a locked
  release `napi_wasmer` build. A 64 MiB `ArrayBuffer` test forced guest-memory
  growth and validated both end bytes, proving the explicit-store allocator
  path still grows correctly.
- Passed the exact EdgeJS regressions for HTTP timeout, drained-socket agent
  reuse, diagnostics-channel module import, buffer construction, HTTP proxy
  fetch, and VM context execution. The drained-socket regression then passed
  100 consecutive repetitions. A 5,000-call `process.report.getReport()`
  microbenchmark averaged 490.42 ms with scoped reentry versus 499.03 ms with
  generic store lending (about 1.7% faster in that mixed workload).
- The exact final V8+WASIX suite passed 1,675 tests. Its only failure was
  `sequential/test-https-connect-localport.js` with `EADDRNOTAVAIL`; the same
  test immediately reproduced with the untouched `1add135` baseline runner,
  so it is pre-existing WASIX `localPort` behavior rather than a reentry
  regression. An earlier full run's sole HTTP-timeout failure coincided with a
  roughly two-day host-clock jump while the machine slept; its focused rerun
  passed.
- Published scoped reentry as N-API commit `f44087e`, advanced Wasmer SDK PR
  #6956 to that gitlink in `ed1ed30281f`, and advanced EdgeJS PR #149 to the
  same N-API revision in `5170173c`. The Wasmer stack remains intact: its
  nested store-context fix can still be useful to Wasmer independently, while
  N-API no longer pays the generic parked-store cost.

## Shared memory versus store reentry

The property-enumeration guard and the guest heap's need for a store are related
in the current native implementation, but they are not the same requirement:

- Wasmtime models `SharedMemory` as an engine-owned, clonable `Send + Sync`
  allocation. It may be imported into multiple stores and exposes store-free
  byte access, atomics, and growth. Ordinary functions, tables, globals, and
  store state remain store-owned and require an exclusive store context.
- V8 permits a shared WebAssembly backing store to be observed by multiple
  isolates, but each `WebAssembly.Memory` wrapper and every guest invocation is
  still isolate-owned. An isolate may be entered by only one thread at a time;
  crossing threads requires the Locker/Unlocker protocol. Wasmer's V8 backend
  deliberately enforces the store's creator thread and represents sharing as a
  detached handle that must be attached to the destination store. It does not
  support store-free shared-memory growth.
- Wasmer's unmerged `fix/shared-memory-store-free-grow` implementation follows
  that restriction: `SharedMemory::grow`, `data_ptr`, and `style` work without
  a store only for the `sys` backend; V8 and JavaScript report unsupported.

Consequently, a store-independent shared-memory API can remove the guest heap's
store dependency on `sys`, but it cannot replace the callback context around
`napi_get_property_names`. A Proxy `ownKeys` trap is guest code: dispatching it
needs the guest table/function environment and, on native Wasmer backends
including V8, the same executing store. The browser JavaScript backend does not
use `Store::with_current` here; its same-named guard installs a synchronous
`FunctionEnvMut` callback context (or uses the persistent async context). The
guard is therefore required on both paths, while actual store lending is a
native implementation detail.

Any later cleanup should make those two responsibilities explicit in naming or
helper structure:

1. install guest callback/reentry context around every host operation that may
   synchronously execute user JavaScript and call a guest callback;
2. lend the current store only in the native implementation that must recover
   it through foreign V8/C++ frames;
3. use store-free `SharedMemory` operations only where the backend explicitly
   supports them, never by weakening V8's isolate/thread checks or sending a
   store-owned V8 memory wrapper across threads.

## Reentry simplification and cost model

The former generic native guard was cheaper than its name suggested, but more
generic than N-API needed. `with_callback_state` stack-allocated the invocation
context; the steady-state path did not allocate a box or take a mutex. Its
per-boundary cost was an atomic active-context exchange plus a
`RefCell<Vec<_>>` push/pop in Wasmer's store-context TLS. An actual nested
callback additionally cloned the `FunctionEnv` handle and recovered the store
through `Store::with_current`.

There is a simpler precedent in this repository. The native implementation
before `0e5d3f2` stored a scoped pointer to the current `FunctionEnvMut` in the
callback invocation context, and the current JavaScript backend still uses that
model. The change to generic store lending was primarily needed so foreign V8
ArrayBuffer allocator frames could recover a store when guest-heap allocation
had to grow memory; guest callback dispatch itself does not require a global
store stack when the active `FunctionEnvMut` can be passed directly.

A staged simplification was implemented as follows:

1. Restored a stack-scoped direct `FunctionEnvMut` token for native synchronous
   callback reentry. The callback trampoline can invoke the guest function
   directly through that token, removing `Store::with_current` and the
   `FunctionEnv` clone from nested callbacks. The implementation enforces the
   same-thread restriction and was validated by native, V8 WASIX, and
   JavaScript-backend checks.
2. Added an explicit `GuestHeap::alloc_with_store` path for imports that already
   own `FunctionEnvMut`. They no longer publish and rediscover the same store
   merely to grow memory.
3. Kept allocator-driven growth separate. V8 documents its ArrayBuffer
   allocator as thread-safe and forbids calling back into V8 from allocator
   methods, so an active store pointer must never be dereferenced by a
   background allocator thread. On the owning foreground thread, it uses a narrow,
   on-demand growth token; background `Free` remains store-independent and a
   wrong-thread `Allocate` may use already-reserved arena capacity or fail, but
   must not enter the Wasmer V8 store.
4. Deferred store-free `SharedMemory` growth on `sys` until Wasmer exposes it.
   That future optimization can remove the allocator's store dependency for
   that backend. It must not be emulated for V8 by bypassing isolate/thread
   ownership.
5. Removed `StoreMut::parked`/`Store::with_current` from the N-API hot path
   after the allocator no longer needed the generic mechanism. The nested
   store-context borrow fix may remain independently useful inside Wasmer's
   normal function-call machinery.

This design is closer to V8's explicit callback-data model: persistent callback
metadata identifies the guest function, while a short-lived invocation token
supplies the current execution context. It avoids paying the generic Wasmer TLS
store-publication cost for calls that never allocate guest backing memory, while
retaining a narrowly scoped fallback for the cases that really can grow it.

As a smaller cleanup, `napi_get_global` was removed from the guarded set after
confirming that its bridge does not execute JavaScript. Property access,
property enumeration, function/script execution, module evaluation, and
teardown remain legitimate guards because JavaScript accessors, Proxies,
evaluation, or finalizers can synchronously reenter the guest.

## Verification expectations

- No unresolved conflict markers or unmerged index entries.
- `git diff --check` and Rust formatting pass.
- Guest heap tests cover arena-only allocation, explicit-store growth, and
  unregistered-store rejection.
- Host-control tests cover termination, memory-accounting release, budget-floor
  rejection, and V8 worker configuration.
- Both N-API CI workflows complete successfully and GitHub reports the PR
  mergeable against current `main`.
