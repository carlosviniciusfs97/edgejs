# Host-JavaScript Imported N-API Package

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟢 | `syrusakbary/edgejs@0.1.9` passes aliases, pnpm install, Next.js dev startup, and browser preview with the host-JavaScript backend. |
| **Severity** | High | This package is the engine-free guest required for the browser/Node performance path. |

## Goal

Publish the existing imported-N-API WASIX build as
`syrusakbary/edgejs`, without QuickJS or V8 linked into the WebAssembly module.
A modified Wasmer host will satisfy the imports using the browser or Node.js
JavaScript engine through its WebAssembly/wasm-bindgen integration.

## Subtasks

- [001_package_contract.md](001_package_contract.md): package identity, build
  boundary, and release contract.
- [002_artifact_validation.md](002_artifact_validation.md): binary import and
  embedded-engine validation.
- [003_runtime_status.md](003_runtime_status.md): published release, smoke-test
  results, and remaining runtime blockers.
- [004_buffer_ownership.md](004_buffer_ownership.md): the persistent-control,
  scoped-bulk-data, and synchronous-compression ownership contract.
- [`SECURITY-HOST-JS-NAPI.md`](../../../../SECURITY-HOST-JS-NAPI.md): deliberately
  deferred security concerns and later exit criteria.

## Scope boundary

This repository owns the EdgeJS guest module and package. Wasmer owns the
host-JavaScript N-API implementation, JSPI integration, worker lifecycle, and
SDK exposure. The runtime milestone covers CommonJS, ESM, files, buffers,
promises, timers, workers, networking, HTTP, and package installation.

The engine-free package is explicit and does not replace the separately
published embedded-QuickJS package.

## Verification

The final 53 MiB optimized artifact passed with 111 standard N-API function
imports and 91 Wasmer extension function imports. The validator fixture suite
passed all six tests, and `wasmer package build --check .` accepted the package
manifest. Node's native `WebAssembly.Module` validation also passes after
disabling LLVM's experimental wide-arithmetic proposal. The package is
published as `syrusakbary/edgejs@0.1.9`. The package exports `edge`, `edgejs`,
and `node`; both aliases pass `--version` in a registry-backed `wasmer-sh`
browser session.
