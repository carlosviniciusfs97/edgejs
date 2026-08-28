# Verification and handoff

| Field | Value |
| --- | --- |
| Scope | Verify that V8 WASIX CI no longer provisions Wasmer for tests and still exercises the imported-N-API artifact. |
| Dependencies | Subtask 002 implementation. |
| Write ownership | Development/troubleshooting notes and registries only. |
| Status | 🟢 Complete. |
| Verification | Structural CI checks plus the strongest locally practical standalone CLI runtime checks. |

Record commands and results here. If a full WASIX suite cannot run locally,
state the missing toolchain or runtime prerequisite precisely and retain
targeted coverage for every changed adapter.

## Results

Completed on 2026-08-28:

- `make build-wasix`: passed; produced an engine-free artifact with 110
  standard N-API imports and 67 extension imports.
- `make validate-wasix-imports`: passed against `./napi`, proving the pinned
  standalone provider covers the artifact's standard and extension imports.
- `python3 wasix/test_validate_imported_napi_wasm.py`: 8 tests passed.
- `RUSTUP_TOOLCHAIN=1.95.0 make build-napi-wasmer-cli`: passed in locked release
  mode with Cranelift and no Wasmer CLI/custom LLVM installation.
- Direct CLI smoke: guest cwd, environment, program name, builtins, mounts, and
  direct stdout all passed.
- Node runner focused tests passed for Buffer, filesystem reads, and a local
  HTTP client/server (`test-buffer-alloc.js`, `test-fs-readfile.js`, and
  `test-http-1.0.js`).
- Framework runner smokes passed for nested guest cwd/environment propagation
  and for an EdgeJS-root absolute script path remapped through `/workspace`.
- `make test-wasix-v8-intl`: passed all five locale tests (with the existing
  old-ICU skip).
- `make test-wasix-v8-lang`: passed both guest-finalizer tests.
- Shell syntax, workflow YAML parsing, Cargo formatting, Make dry-runs, and
  root/N-API `git diff --check` passed.

Follow-up verification after the Rust 1.94 CI lockfile failure:

- Rust 1.95 locked release build against Wasmer SDK revision `5281e55`: passed.
- `x86_64-unknown-linux-gnu` locked metadata resolution: passed.
- Direct `napi_wasmer build-wasix/edgejs.wasm -e ...` smoke: passed and printed
  `hello world!`.
- Imported-N-API validation: 110 standard imports and 67 extension imports.
- Import-validator tests: 8 passed.

The complete Node and framework matrices remain for GitHub Actions; focused
local coverage exercised every changed runner path and the failure point from
run `33196256897` no longer exists.
