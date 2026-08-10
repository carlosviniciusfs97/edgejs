# Runtime Status

## Candidate artifact

- Package: `syrusakbary/edgejs@0.1.9` (published)
- Engine: host-provided N-API; no embedded QuickJS or V8
- Imports: 111 `napi` functions and 91 `napi_extension_wasmer_v0` functions
- Compatibility: standardized `exnref` WebAssembly exception handling and no
  experimental wide-arithmetic, targeting current Chrome and Node V8 engines
- Commands: `edgejs`, `edge`, and `node`

## Validation completed

- Full engine-free WASIX build and imported-N-API validation
- `wasmer package build` for the exnref candidate WebC
- Wasmer SDK production WebAssembly build
- Native and browser-target Wasmer/N-API compile checks
- Browser `wasmer-sh` smoke for `node --version` and `edge --version`
- Browser `worker_threads` parent/child message round trip
- Browser native filesystem callback followed by a Promise/`await` continuation
- Browser `pnpm install --frozen-lockfile --ignore-scripts` with normal network
  concurrency, followed by loading the installed Express package

## Cross-worker structured-clone transport

The temporary JSON/base64 graph serializer was removed. Unofficial N-API
message serialization publishes an opaque handle through Wasmer's existing
C-API worker bridge. `Worker.postMessage()` performs the structured clone in
both browser and Node hosts; the receiving N-API environment consumes the
cloned value by handle. EdgeJS retries when a native `uv_async` wake races ahead
of host delivery rather than emitting a spurious `messageerror`.

Forward startup delivery and the child-to-parent reply are both covered by the
worker smoke. Wasmer retains one central structured-clone value until Edge
releases its opaque handle. A worker missing that handle requests it from the
scheduler, which sends a clone only to that worker. This avoids both global
broadcast and the earlier blocking `Atomics.wait()` acknowledgement. Security
and lifecycle work required before multi-tenant use is recorded in
`SECURITY-HOST-JS-NAPI.md`.

## Promotion status

The browser `wasmer-sh` pnpm install gate passes against the clean local
`syrusakbary-edgejs-0.1.8-ready2.webc` artifact. The fix establishes a buffer
synchronization boundary around every completed native callback, including the
synchronous callback path used by Node's pooled Buffers. Registry response
chunks and tarballs now retain their bytes through native calls, worker
structured cloning, integrity verification, and extraction.

`syrusakbary/edgejs@0.1.9` is published. The full browser gate also passes with
`wasmer-sh` resolving that package from the registry, without a local WebC
override: fetch 16.2 s, offline install 4.8 s, Next dev ready 1.7 s, and preview
rendered in 5.1 s.

The implementation must continue to preserve these constraints:

- no changes under EdgeJS `lib/`
- host-owned bulk buffers use explicit scoped native access; persistent native
  control blocks use guest-backed typed arrays
- no custom value serializer
- Wasmer dependency from `/Users/syrusakbary/Development/wasmer`
- exnref exception handling
