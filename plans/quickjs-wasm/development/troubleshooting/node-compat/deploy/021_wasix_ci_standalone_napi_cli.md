# WASIX CI standalone N-API CLI

| | | Remarks |
| --- | --- | --- |
| **Status** | 🟢 | The standalone runner builds from its lockfile with Rust 1.95 against the current compatible Wasmer SDK revision and passes the direct WASIX smoke. |
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

## 2026-08-28 regression

PR #149 reaches the standalone runner build but Cargo 1.94 rejects the copied
`Cargo.standalone.lock` before compilation:

```text
error: cannot update the lock file .../.napi-cargo-standalone.*/Cargo.lock
because --locked was passed to prevent this
```

The checked-in standalone manifest currently pins Wasmer revision
`60ea058b2634ae4ed46cc14fc8b49e6b5304e7f0`, while the requested current head
of `codex/js-napi-host-reentry-sdk` is
`5281e55dac6c85be91560c4c828d49fd2d5b8b5d`. The lockfile and N-API source must
be validated together against that SDK revision rather than bypassing
`--locked`.

### Regression action plan

1. Reproduce the exact Cargo 1.94 locked-build failure from the materialized
   standalone manifest.
2. Pin every standalone Wasmer dependency to the current immutable commit from
   `codex/js-napi-host-reentry-sdk` and regenerate the dedicated lockfile with
   the CI Rust toolchain.
3. Compile `napi_wasmer`, adapting N-API only where the SDK branch changed its
   APIs or runtime contract.
4. Run the direct WASIX smoke and the focused import/runner checks, then restore
   this issue to done only after the locked build passes from a clean temporary
   materialization.

### Regression resolution

- Published the Wasmer branch's existing N-API compatibility commit
  `4cd5d42cf1794c6cd53a04194c968b2a7588eec0` on the N-API branch
  `codex/js-napi-host-reentry-sdk`. The Wasmer git dependency could not resolve
  while its `lib/napi` gitlink named an unpublished commit.
- Pinned every dependency in `Cargo.standalone.toml` to immutable Wasmer SDK
  revision `5281e55dac6c85be91560c4c828d49fd2d5b8b5d` and regenerated
  `Cargo.standalone.lock` with Cargo 1.95.
- Updated N-API to the Wasmer 7.3/0.703 package line and applied the SDK
  branch's static-memory API adaptations in the resource-budget and guest-heap
  code.
- Kept the memory-tunables adapter explicitly native: every `wasm32` build uses
  Wasmer's JavaScript backend, so `BudgetedTunables` and its `wasmer::sys`
  imports are now gated on `not(target_arch = "wasm32")`. The helper no longer
  accepts an unused engine target. Host-JavaScript builds retain only the
  shared provider budget used for environment, value-handle, and declared
  external-memory limits.
- Moved the N-API toolchain from Rust 1.94 to 1.95, the minimum version declared
  by the pinned Wasmer SDK crates.
- Published the complete standalone-runner update as N-API commit
  `baafd3e` on `codex/edgejs-wasix-sdk-runner`.

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
- The reopened regression was verified with a clean materialization of the
  standalone manifest: `make build-napi-wasmer-cli` passed with `--locked` in
  release mode on Rust 1.95.
- Cargo metadata resolved the same lockfile for
  `x86_64-unknown-linux-gnu`, covering the platform from the failing CI job.
- The simplified sys boundary passes both the locked native release CLI build
  and `cargo check --locked --target wasm32-unknown-unknown --features
  js,wasix --lib`; the latter does not compile or install budgeted tunables.
- The rebuilt runner executed `build-wasix/edgejs.wasm` and printed
  `hello world!`.
- Provider validation still reports 110 standard N-API imports and 67
  extension imports, and all 8 validator tests pass.
