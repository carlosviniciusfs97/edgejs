# Standalone N-API CLI contract audit

| Field | Value |
| --- | --- |
| Scope | Confirm the checked-in `napi_wasmer` CLI contract and map the V8 WASIX runners to it. |
| Dependencies | Current EdgeJS `main`; N-API standalone-runner update `baafd3e`. |
| Write ownership | Read-only audit; no repository files. |
| Status | 🟢 Complete. |
| Verification | Identify build target, binary path, argument/mount/env/cwd support, and incompatible Wasmer-only flags. |

The audit must distinguish functionality provided by the standalone CLI from
functionality currently supplied by `wasmer run`. It should recommend the
smallest EdgeJS-owned adapter needed for the Node, locale, framework, and
standalone-build test lanes.

## Findings

- The runner is `napi/src/bin/napi_wasmer.rs`, built with the N-API
  repository's standalone Cargo manifest.
- N-API and WASM C-API imports are built into the runner; `--experimental-napi`
  and package-level Wasmer resolution are not applicable.
- Host networking is supplied by the embedded WASIX runtime; the standalone
  CLI does not need a `--net` switch.
- The EdgeJS test adapters need repeatable environment variables, a guest
  working directory, a stable guest program name, mounts, guest arguments, and
  direct stdio. The existing CLI already had mounts and guest arguments; the
  remaining options were added to its public runner helper and command line.
- The pinned N-API source requires the Wasmer 7.3 APIs from SDK revision
  `5281e55dac6c85be91560c4c828d49fd2d5b8b5d`. Published crates do not expose
  the complete matching hook/shared-memory API, so the standalone manifest
  locks that immutable library revision.
- Cranelift is sufficient for the test runner and avoids the custom LLVM
  installation previously hidden inside Wasmer CLI provisioning.
