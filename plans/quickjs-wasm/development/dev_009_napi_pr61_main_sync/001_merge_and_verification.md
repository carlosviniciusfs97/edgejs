# N-API PR #61 main sync and verification

| | | Remarks |
| --- | --- | --- |
| **Status** | ▶️ | N-API is synced and the Wasmer stack is combined; EdgeJS and GitHub CI verification remain active. |
| **Scope** | N-API PR #61, Wasmer PR #6956, EdgeJS PR #149 | Guest heap store lending, callback reentry, SDK integration, resource budgets, host termination, and V8 worker configuration. |

## Dependencies

- PR branch: `fix/guest-heap-store-lending` at `6c3ea892` before this sync.
- Base branch: current `wasmerio/napi:main`.
- Wasmer APIs used by the PR: `StoreMut::parked`, `Store::with_current`, and
  the stacked Wasmer changes in PRs #6886, #6887, and #6888.
- Existing EdgeJS and nested QuickJS working-tree changes must remain untouched.

## Write ownership

- Perform all N-API merge and conflict edits in an isolated Git worktree.
- Push only the PR's own `fix/guest-heap-store-lending` branch.
- Update this task note from the EdgeJS root after the N-API branch is stable.
- The follow-up request explicitly authorizes advancing the EdgeJS N-API
  gitlink and SDK pin after the combined Wasmer branch is stable.

## Action plan

1. Fetch N-API `main` and the PR head, then create an isolated worktree at the
   PR head.
2. Merge `origin/main` into the PR branch without rewriting its published
   history.
3. Resolve conflicts by preserving the PR's store-lending and host-control
   invariants while adopting current main's standalone CLI, target-specific
   budgeting, manifest, and test changes.
4. Inspect the complete post-merge diff against `main` for accidental scope
   expansion and run formatting and whitespace checks.
5. Run the focused library tests introduced by the PR, followed by the native
   V8 and QuickJS CI-equivalent suites supported by the repository.
6. Push the resolved branch, monitor all PR checks, and address any failure
   until the PR is green and mergeable.

## Integration progress

- Merged current N-API `main` into `fix/guest-heap-store-lending` as
  `d6dd23a`, resolving five manifest/runtime conflicts without expanding the
  effective PR diff beyond the guest-heap and host-control work.
- Merged the complete Wasmer #6886 → #6887 → #6888 stack into
  `codex/js-napi-host-reentry-sdk` as `57f98264b27`, resolving the N-API
  submodule to the synced PR #61 head.
- Verified the combined Wasmer workspace with 4 store-lending API tests, 44
  N-API library tests, and 244 passing WASIX library tests (2 ignored).
- Pinned N-API standalone builds to immutable Wasmer commit `57f98264b27` and
  pushed N-API head `99a58dd`; the locked standalone library suite passes all
  44 tests.
- Advanced Wasmer SDK PR #6956 to N-API head `99a58dd` in `83cf85c3292`.
- The first EdgeJS V8 WASIX smoke exposed one missed reentry boundary:
  `unofficial_napi_contextify_run_script` ran JavaScript while its import still
  held the store. A scoped trace showed the callback trampoline reaching a
  store-context entry with one live borrow. N-API commit `4409705` now routes
  only that execution bridge through the existing `with_cb_context` guard.
- Advanced Wasmer SDK PR #6956 to the contextify fix at `7f091a44738` and the
  EdgeJS PR worktree to N-API head `4409705`.
- Rebuilt the complete V8 WASIX guest, validated its 110 N-API and 67 extension
  imports, rebuilt the locked release `napi_wasmer` runner, and passed the
  Makefile smoke with `hello world!`.
- Fixed the SDK stack's Rust 1.95 lint and NodeJS test-feature regressions in
  Wasmer commit `1e295461b57`. The exact NodeJS WASM test build and full
  `make lint-packages` target pass locally. The dependency cooldown advisory is
  intentionally out of scope because crates.io yanked the eligible
  `chacha20` releases while their replacement remains inside the repository's
  three-day quarantine.
- Pinned the N-API standalone manifest and lockfile to that latest SDK commit
  in N-API head `625777a`, then advanced EdgeJS PR #149 to that N-API head.
  The locked standalone N-API library suite still passes all 44 tests.
- Applied the exact Taplo formatting required by Wasmer CI in N-API commit
  `1e11604` and Wasmer SDK commit `5a02e54a540`, then advanced EdgeJS to the
  formatting-complete N-API head. These follow-up commits contain no runtime
  behavior changes.

## Verification expectations

- No unresolved conflict markers or unmerged index entries.
- `git diff --check` and Rust formatting pass.
- Guest heap tests cover arena-only allocation, lent-store growth, and
  unregistered-store rejection.
- Host-control tests cover termination, memory-accounting release, budget-floor
  rejection, and V8 worker configuration.
- Both N-API CI workflows complete successfully and GitHub reports the PR
  mergeable against current `main`.
