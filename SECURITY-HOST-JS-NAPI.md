# Deferred Security Concerns: Host-JavaScript N-API

The `syrusakbary/edgejs` package intentionally imports N-API and executes
JavaScript in the host's JavaScript engine. Security hardening is deferred for
the first performance/compatibility milestone. This file records the issues so
they are not lost; none of them currently blocks the package.

## Concerns to address before calling this mode a security boundary

- The host JavaScript realm may expose ambient authority such as `fetch`,
  `WebSocket`, storage, timers, `process`, or host module loaders beyond WASIX
  capabilities granted to the guest.
- Running the module in a Web Worker or Node worker is useful isolation for
  scheduling and teardown, but it is not itself a security sandbox.
- Dynamic `import()` and CommonJS loading must not accidentally route through
  the browser's or Node.js host loader and escape the guest filesystem/package
  policy.
- Browser CSP requirements may conflict with generated WebAssembly/JavaScript
  glue, dynamic code compilation, or source evaluation.
- CommonJS functions currently retain their N-API environment through a
  generated lexical scope. That scope and its dynamic `with` lookup must be
  treated as a compatibility mechanism, not as a security boundary; hardening
  should replace it with an explicitly controlled realm implementation.
- Host-native globals and prototypes need an explicit allowlist and attenuation
  strategy; deleting a few globals is not sufficient isolation.
- Values, callbacks, exceptions, promises, and typed-array backing stores that
  cross N-API can leak host-realm objects or capabilities into guest code.
- Cross-worker N-API values are published through Wasmer worker
  `postMessage()` calls. The scheduler retains a central clone and sends it to
  the worker that requests the opaque handle. Before multi-tenant use, bind
  those handles structurally to a sandbox and destination, prevent
  guessing/replay, bound retained values, and prove cleanup on worker
  termination and failed delivery.
- Multi-tenant use needs a clear realm lifecycle, cleanup, and non-reuse policy
  so state cannot leak between EdgeJS invocations.
- Resource controls (CPU interruption, memory, microtask starvation, callback
  recursion, and event-loop liveness) must be enforced independently of WASIX
  filesystem/network capabilities.
- N-API value IDs currently remain strongly retained until environment teardown
  because Edge can revisit values after their native handle scope closes. Bound
  this table or add an explicit retain/release ABI so hostile or long-running
  workloads cannot turn stable handles into unbounded host-memory retention.
- The browser WISP bridge currently permits up to 128 MiB of received data per
  socket so large package archives are not truncated while the guest is busy.
  Add receive-side flow control and aggregate per-sandbox quotas before treating
  this as safe against memory-exhaustion workloads.

## Follow-up exit criteria

Before treating host-JavaScript mode as secure, define a threat model, test the
authority boundary in browsers and Node.js, document CSP requirements, add
realm-escape regression tests, and complete an external security review. Until
then, use the embedded-engine package for workloads that rely on a JavaScript
engine sandbox.
