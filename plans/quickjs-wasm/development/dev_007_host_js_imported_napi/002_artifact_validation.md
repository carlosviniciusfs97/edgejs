# Artifact Validation

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟢 | Validator, fixture tests, build integration, and Make targets are implemented. |
| **Severity** | High | A provider or linker regression would silently turn this into the wrong package. |

`wasix/validate-imported-napi-wasm.py` parses the WebAssembly import section
without external Python packages. It verifies:

- standard N-API function imports exist under `napi`;
- EdgeJS unofficial extension imports exist under
  `napi_extension_wasmer_v0`;
- N-API functions are not accidentally imported from `env` or another module;
- known QuickJS and V8 native symbol sentinels are absent;
- the CMake cache, when supplied, selected the `imports` provider.

The build invokes the validator after optimization and copies the validated
module to `build-wasix/edgejs.wasm`. Fixture tests cover a valid module,
missing standard and extension imports, a misplaced import, an embedded-engine
sentinel, and malformed input.

## Commands

```sh
make test-wasix-import-validator
make build-wasix
make validate-wasix-imports
```

Binary symbol scanning is a regression guard, not a proof that a particular
engine implementation is absent. The explicit CMake provider check plus the
WebAssembly import contract is the primary guarantee; the sentinels catch
common accidental static-link regressions.

## Verification result

The complete WASIX build passed the integrated validator:

```text
validated engine-free EdgeJS wasm: 111 N-API imports, 91 extension imports
```

The browser-compatible optimized `build-wasix/edgejs.wasm` is about 53 MiB.
All six validator fixture tests passed, Node accepted the module, and
`wasmer package build --check .` completed successfully.
