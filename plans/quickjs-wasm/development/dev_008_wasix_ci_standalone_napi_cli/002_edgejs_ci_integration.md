# EdgeJS CI integration

| Field | Value |
| --- | --- |
| Scope | Build and select `napi_wasmer` for V8 WASIX tests without provisioning the Wasmer CLI or source checkout. |
| Dependencies | Subtask 001 CLI contract audit. |
| Write ownership | Root `Makefile`, V8 WASIX runner integration, `.github/workflows/test-and-build.yml`, and directly related docs/tests. |
| Status | 🟢 Complete. |
| Verification | Workflow syntax, Make dry-runs, runner syntax/tests, standalone CLI smoke, and import-validator tests. |

Keep the WASIX artifact build and distribution path unchanged. Only the test
runtime should move from the external Wasmer CLI to the N-API repository's
standalone CLI. Publishing may still install Wasmer because publishing is not a
test operation.

## Implementation

- The root `Makefile` builds a locked release `napi_wasmer` binary and makes it
  a prerequisite of the V8 WASIX Node, locale, language, framework, and
  standalone-build test targets.
- The Node and framework runner scripts have an explicit `napi` mode that
  passes the raw `build-wasix/edgejs.wasm` module, host mounts, environment,
  guest cwd, program name, and guest arguments directly to that binary. Their
  existing Wasmer mode remains available for QuickJS/package workflows.
- The `v8-wasix` workflow no longer resolves, restores, builds, downloads, or
  caches a Wasmer CLI/source checkout. It validates the artifact against the
  exact pinned N-API provider and builds the standalone CLI instead.
- The N-API standalone manifest has its own lockfile, uses an immutable
  compatible Wasmer library revision, and selects Cranelift. Runtime compiled
  module caching uses a stable temporary cache directory instead of resurrecting
  the ephemeral standalone Cargo materialization directory.
- N-API-side changes are recorded in submodule commit
  `0c38589f361811cc0e2e9e86c1dc9277923887a0`.
- Nightly publishing still installs Wasmer because `wasmer publish` is a
  distribution operation, not a test-runtime dependency.
