# N-API Surface Reduction PR Review Checkpoints

| | | Remarks |
| --- | --- | --- |
| **Status** | ✅ | Checkpoint A is implemented and verified locally on the Edge.js and N-API `codex/napi-surface-reduction` branches. |
| **PRs** | [napi#59](https://github.com/wasmerio/napi/pull/59), [edgejs#147](https://github.com/wasmerio/edgejs/pull/147) | Review feedback is handled by ownership boundary, not comment order. |
| **Invariant** | Provider-neutral Edge logic | `edgejs/lib` remains unchanged; Edge must not select providers with `#ifdef __wasi__`. |

## Checkpoint A implementation status

| Boundary | Status | Implementation |
| --- | --- | --- |
| A1: wire descriptors | ✅ | Native descriptors are separated from fixed-width wasm32 layouts, with compile-time C/C++ and Rust size checks and named Rust offsets. |
| A2: ownership and hooks | ✅ | Guest heaps are rejected or transferred explicitly; providers return an accepted hook mask; Edge-owned cleanup and destroy callbacks were removed from the provider contract. |
| A3: runtime lifetime | ✅ | A versioned, explicit process-runtime configuration call owns engine flags. Environment creation contains only per-environment state, and workers cannot configure the runtime. |
| A4: compatibility gate | ✅ | The validator compares all Edge standard and extension imports with both Wasmer N-API provider implementations, CI records the revision triplet, and the rebuilt SDK-branch Wasmer instantiates the resulting Edge artifact. |

The runtime is intentionally modeled as one explicitly configured process
singleton rather than as multiple opaque handles. That matches V8's actual
lifetime, prevents the first environment from choosing process flags, and
avoids pretending that independently releasable runtimes exist where they do
not.

## Decision

The reduction is directionally correct, but exported function count is not the
primary success metric. The boundary is successful only when each operation
has one semantic owner, fixed lifetime rules, and a representation that cannot
silently change between native and wasm32 builds.

The review exposed four boundaries which must be made explicit before further
surface reduction:

1. native C ABI versus the wasm32 wire representation;
2. process-global runtime configuration versus per-environment state;
3. required embedder callbacks versus optional or unsupported callbacks; and
4. the compatible Edge/N-API/Wasmer protocol version triplet.

## Checkpoint A: protocol, ownership, and runtime lifetime

Checkpoint A is complete only when the following are true.

### A1. Fixed-width wire descriptors

- Native public descriptors retain natural C types where they represent native
  addresses or sizes.
- Every descriptor decoded from guest linear memory has a fixed-width wasm32
  wire definition. Wire pointers, handles, sizes, and lengths are `uint32_t`;
  counters which are genuinely 64-bit remain `uint64_t`.
- Rust decoders do not contain unexplained numeric offsets. Each offset is tied
  to a named wire layout and checked by compile-time C/C++ and Rust layout
  assertions.
- Descriptor `size` and `version` are validated before reading or taking
  ownership of any later field.
- The bytecode version begins at version 1 unless an older version was actually
  shipped and remains supported.

The purpose is not to introduce a generic opcode protocol or bindgen dependency.
Provider implementations remain typed and handwritten. The wire layer exists
only where a wasm32 pointer crosses into Wasmer.

### A2. Resource ownership and hook capability

- `guest_heap` has one documented owner on every success and failure path.
- A bridge which cannot accept a transferred resource rejects it before
  ownership transfer; it never ignores the pointer.
- `attach_env` must not report success for required callbacks it discards.
- Callbacks controlled by Edge's own lifecycle should be removed from the
  provider contract instead of being advertised as provider capabilities.
- Remaining provider-driven hooks are attached in one transaction. The
  provider either accepts the required set or returns an explicit failure.

### A3. Process runtime versus environment lifetime

- V8 flags and other process-global initialization are not configured by an
  arbitrary first environment.
- Process/runtime configuration has a typed owner and is initialized once;
  environments are created from that initialized runtime.
- Identical repeated configuration is idempotent. Conflicting configuration is
  rejected before any environment is partially created.
- Worker creation cannot change or accidentally become the source of the
  process-global configuration.
- QuickJS and host-JavaScript providers implement the same lifetime contract,
  even when their runtime owner is lightweight.

### A4. Protocol compatibility gate

- The Edge wasm import inventory is compared with the Wasmer provider exports
  before the slow compatibility suite starts.
- CI records the exact Edge, N-API, and Wasmer SDK revisions used together.
- A newly added operation must be implemented or explicitly rejected in V8,
  QuickJS, the native Rust bridge, and the host-JavaScript bridge before N-API
  CI can be green.

### Verification for Checkpoint A

1. N-API descriptor-validation and ownership tests for V8 and QuickJS.
2. A wasm32 layout/conformance test for every versioned wire descriptor.
3. Runtime tests covering identical configuration, conflicting configuration,
   main-environment-first, and worker/environment ordering.
4. Wasmer native and host-JavaScript bridge compilation.
5. Edge native V8 and QuickJS targeted environment/worker tests.
6. Edge WASIX import-conformance and instantiation smoke tests using the pinned
   Wasmer SDK branch.

Local evidence captured on 2026-08-14:

- V8 and QuickJS native provider builds pass.
- The focused V8 N-API suite passes 30/30 tests; the focused QuickJS N-API
  suite passes 28/28 tests.
- Edge V8 and QuickJS version/evaluation smokes pass, including worker teardown
  in both providers.
- All eight import-validator unit tests pass.
- The engine-free Edge WASIX artifact builds and validates 110 standard N-API
  imports plus 67 extension imports against both Wasmer provider inventories.
- The Wasmer N-API crate compiles against
  `/Users/syrusakbary/Development/wasmer` natively and for
  `wasm32-unknown-unknown` with the host-JavaScript feature.
- A clean worktree of the Wasmer SDK branch rebuilt against this N-API tree
  instantiates `build-wasix/edgejs.wasm` with `--experimental-napi`; `--version`
  reports `v24.13.2-pre`.
- `git diff --check`, Rust formatting checks, and the no-`edgejs/lib`-changes
  invariant pass.

## Checkpoint B: observable behavior

Checkpoint B fixes behavior changed while adopting the reduced protocol:

- QuickJS heap statistics and centralized `valid_fields` normalization;
- profiling start-result consistency;
- requested-field error metadata without unnecessary JS re-entry;
- unified sync/async `--abort-on-uncaught-exception` policy;
- iterative HTTP/2 input consumption;
- native tick-callback trampoline and transactional reference replacement;
- exact call-site frame counts;
- worker-thread-affine profiler ownership;
- DOMException bootstrap fallback;
- metadata-header parsing without a fixed line limit; and
- honest WASIX total/free/available memory reporting.

Each fix requires a targeted regression test before the wider suites run.

## Checkpoint C: packaging and scope separation

- Keep the intentional package identity `syrusakbary/edgejs` and the `edge`,
  `edgejs`, and `node` commands, and document that decision in the PR.
- Move npm out of the Node test submodule into a pinned production asset.
- Keep release-only version bumps separate from protocol commits.
- Move Next.js/WebContainer compatibility commits into their own follow-up PR.
- Publish a removed-operation mapping, provider capability matrix, ownership
  table, exact revision triplet, and test evidence in the PR descriptions.

## Non-goals

- No changes under `edgejs/lib`.
- No provider selection in Edge runtime logic.
- No generic `unofficial_napi_call(opcode, ...)` dispatcher.
- No compatibility stub which claims to implement an unsupported capability.
- No unrelated Next.js, WebContainer, HTTP, parser, or packaging work during
  Checkpoint A.

## Merge order

1. Land Checkpoint A in N-API and update Edge to its exact revision.
2. Pin the compatible Wasmer SDK revision and pass the import gate.
3. Land Checkpoint B as focused correctness commits.
4. Move Checkpoint C follow-ups out of the protocol PR where practical.
5. Merge N-API first, then Edge.js; publication follows the merged revisions.
