# WASIX CI standalone N-API CLI

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟢 | V8 WASIX tests build and use the pinned standalone N-API CLI; external Wasmer provisioning was removed. |
| **Severity** | High | The V8 WASIX job is red before any runtime test and is coupled to a separately provisioned Wasmer source/provider revision. |

## Baseline

GitHub Actions run `33196256897`, job `98934038033`, builds the Edge WASIX
artifact successfully, then provisions a Wasmer CLI/source checkout. Its first
hard failure is the provider compatibility check:

```text
error: Wasmer provider does not implement Edge extension imports:
unofficial_napi_configure_runtime
```

No Node, locale, framework, or standalone-build test starts. The failure is a
test-runtime provisioning mismatch: EdgeJS already pins an N-API submodule that
contains the `napi_wasmer` standalone CLI and should use that CLI as the host
provider for V8 WASIX tests.

## Action plan

1. Audit the standalone CLI's build target, binary path, and runtime arguments.
2. Add an EdgeJS runner mode that maps the existing WASIX test mounts,
   environment, working directory, and guest arguments to `napi_wasmer`.
3. Make V8 WASIX test targets build/use the standalone CLI by default.
4. Remove V8 WASIX test-time Wasmer resolution, download/source build, cache,
   source-provider validation, and revision-triplet workflow steps.
5. Keep Wasmer installation in nightly publishing because publishing to the
   registry is not part of test execution.
6. Verify workflow/runner syntax, focused adapters, import validation, and a
   standalone CLI smoke or suite as locally practical.

## Acceptance gates

- The `v8-wasix` test job has no Wasmer CLI installation or source checkout.
- The WASIX artifact is executed through the N-API submodule's standalone CLI.
- V8 WASIX Node, locale, framework, and standalone-build targets retain their
  test coverage and no longer pass Wasmer-only flags.
- The WASIX distribution artifact and publishing flow remain intact.

## Resolution

The V8 WASIX lane now builds `napi_wasmer` from EdgeJS's pinned `napi`
submodule and executes `build-wasix/edgejs.wasm` directly. The Node and
framework adapters translate their existing mount, environment, cwd, and guest
argument contract to the standalone CLI. Wasmer-only flags such as `--llvm`,
`--net`, `--quiet`, and `--experimental-napi` are no longer used in this lane.

The CLI contract was extended with direct host stdio, repeatable `--env`,
`--cwd`, and `--program-name`. Its standalone dependency graph is locked to an
immutable compatible Wasmer library revision and uses Cranelift, so tests need
neither a Wasmer executable nor the custom LLVM bundle that the old
provisioning path installed.

Provider validation now compares the freshly built engine-free artifact with
`./napi`, the same source used to compile the runner. This retains the ABI
coverage that caught the original mismatch without checking out a second N-API
provider under `.wasmer-src`.

## Verification

- Engine-free WASIX build and pinned-provider import validation passed: 110
  standard N-API imports and 67 extension imports.
- The standalone CLI built in locked release mode and ran the current artifact
  with mounts, environment, guest cwd, program name, direct output, and local
  networking.
- Focused Buffer, filesystem, HTTP, and framework-runner checks passed.
- `make test-wasix-v8-intl` and `make test-wasix-v8-lang` passed.
- The validator's 8 unit tests, shell syntax checks, workflow YAML parsing,
  Cargo formatting, and diff checks passed.
