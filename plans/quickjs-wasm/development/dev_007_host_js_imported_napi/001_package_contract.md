# Package Contract

| Field | Value |
| --- | --- |
| Package | `syrusakbary/edgejs` |
| Version | `0.1.9` |
| Entrypoint | `edge` |
| Commands | `edge`, `edgejs`, `node` |
| Guest artifact | `build-wasix/edgejs.wasm` |
| N-API provider | `imports` |
| Standard import module | `napi` |
| Extension import module | `napi_extension_wasmer_v0` |

The Wasmer registry package existed without a release when this work began.
Version `0.1.6` was the first V8-valid host-engine integration artifact.
Version `0.1.7` added the Node-compatible command aliases used by `wasmer-sh`.
Version `0.1.9` adds the host-JavaScript ownership model and the synchronous
compression continuation fix required for practical package installation.

`wasix/build-wasix.sh` passes `-DEDGE_NAPI_PROVIDER=imports` explicitly and
checks the resulting CMake cache before compiling. QuickJS remains confined to
the separate `quickjs-wasm/` build; V8 is only available through the native
`bundled-v8` provider.

## Release checklist

1. Run `make test-wasix-import-validator`.
2. Run `make build-wasix`; artifact validation is part of the build.
3. Run `make validate-wasix-imports` independently on the final artifact.
4. Run `wasmer package build --check .`.
5. Exercise the CommonJS smoke suite using the Wasmer host-JavaScript backend
   and record any unsupported bridge functions.
6. Publish experimental releases after the compatible Wasmer/SDK runtime
   compiles; do not promote them as a QuickJS replacement until the smoke and
   benchmark gates pass.
