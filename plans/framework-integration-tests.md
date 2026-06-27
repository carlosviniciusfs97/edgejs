# Framework Integration Tests Plan

## Status

This document describes the framework integration test implementation that now
exists in the repo. It replaces the earlier design draft that only covered the
setup phase.

The feature scope for this merge is:

- repo-level framework compatibility testing through `make framework-test`;
- optional single-framework selection;
- a three-layer compatibility matrix:
  - `Node.js`
  - `EdgeJS Native`
  - `Wasmer + EdgeJS Safe`
- per-project logs and matrix summaries that make regressions easy to spot;
- a reset path through `make framework-test-reset`.

## Purpose

The framework test flow validates the JS framework examples in
`wasmer-examples/` as application workloads rather than unit tests.

The current goal is to answer:

1. does the framework boot and serve locally under host `Node.js`?
2. does the same framework boot and serve under native `EdgeJS`?
3. if native `EdgeJS` passes, does it also boot and serve through
   `edge --safe`, i.e. `Wasmer + EdgeJS Safe`?

The intended output is a compatibility matrix, not just a single pass/fail bit.

## Public Interface

The supported operator entrypoints are:

- `make framework-test`
- `make framework-test <framework>`
- `make framework-test-reset`
- `make framework-test-reset <framework>`

The helper script also supports direct invocation:

- `scripts/framework-test.js setup [js-framework-name]`
- `scripts/framework-test.js test [js-framework-name]`
- `scripts/framework-test.js reset [js-framework-name]`

### Supported Environment Variables

- `SYMLINK_TARGET=<path>`
  - selects the comparison runner for the non-Node stages
  - defaults to `build-edge/edge`
- `FRAMEWORK_TEST_PORT_BASE=<port>`
  - defaults to `4300`
- `FRAMEWORK_TEST_PORT_BLOCK_SIZE=<count>`
  - defaults to `10`

## Target Discovery

Framework selection is automatic:

- search `wasmer-examples/` for top-level directories matching `js-*`
- only include entries that contain `package.json`
- sort deterministically
- optionally narrow to one selected framework

If `wasmer-examples/` is missing or uninitialized, the flow fails with an
actionable submodule hint.

## Setup Phase

The setup portion of `framework-test` currently does the following:

1. Creates framework-test state directories under `.framework-test/`.
2. Verifies `pnpm` is available on `PATH`.
3. Resolves the comparison runner from `SYMLINK_TARGET` or `build-edge/edge`.
4. Builds the default EdgeJS binary if the default target is missing and the
   helper script is invoked directly. The `Makefile` entrypoint already depends
   on `build-edge/edge`.
5. Runs `pnpm install --no-lockfile --store-dir .framework-test/pnpm-store` in
   parallel across the selected frameworks.
6. Verifies that each framework has at least one `pnpm` launcher in
   `node_modules/.bin/` that routes through `"$basedir/node"`.
7. Injects `node_modules/.bin/node` so later framework launches run through the
   selected runtime.

### Shim Injection Model

There are now two shim forms:

- single-binary stages use a symlink:
  - `node_modules/.bin/node -> <runner>`
- the safe stage uses an executable wrapper script:
  - `exec <runner> --safe "$@"`

That wrapper is required because safe mode needs an extra CLI layer before the
framework entrypoint.

## Matrix Stages

`make framework-test` now runs a staged compatibility matrix rather than a
single comparison run.

### Stage 1: `Node.js`

- always uses the host `node` discovered from `PATH`
- runs against all selected frameworks
- acts as the baseline for the rest of the matrix

### Stage 2: `EdgeJS Native`

- uses the resolved comparison runner, normally `build-edge/edge`
- only runs frameworks that passed the `Node.js` stage
- reports regressions relative to `Node.js`

If `SYMLINK_TARGET` resolves to the same executable as host `node`, this stage
and the safe stage are skipped.

If `SYMLINK_TARGET` points at a non-default custom runner, the stage label is
`Comparison Runner` instead of `EdgeJS Native`.

### Stage 3: `Wasmer + EdgeJS Safe`

- uses `<comparison-runner> --safe`
- only runs frameworks that passed the comparison stage
- reports regressions relative to the previous stage

This stage is only created when the comparison runner looks like an EdgeJS
binary. If the comparison runner is some other executable, the safe stage is
skipped.

### Matrix Reporting

Each run prints:

- a stage-by-stage pass/fail/skip summary
- a full framework summary matrix
- adjacent-stage regression summaries:
  - `Node.js -> EdgeJS Native`
  - `EdgeJS Native -> Wasmer + EdgeJS Safe`

Skipped frameworks preserve a reason, so later stages explain whether they were
skipped because the prior stage failed or because the prior stage was itself
skipped.

## Runtime Selection And Validation

The runtime phase is fully implemented, not just planned.

### Runtime Script Selection

The harness looks for scripts named:

- `preview`
- `serve`
- `start`
- `dev`
- `develop`

The current scoring prefers more production-style server commands:

- `preview` highest
- then `serve`
- then `start`
- then `dev`
- then `develop`

The score is adjusted upward when the command already appears to support host
and port flags, and downward when it looks development-oriented.

### Build Behavior

The harness runs `pnpm run build` before validation when:

- the project has a `build` script; and
- the chosen runtime script is not obviously dev-only.

Generated framework outputs are then reused by later stages whenever possible,
so `Node.js` does the initial build and the later stages generally validate the
same build output.

### Static Export Handling

Projects that resolve to a Next export flow are treated specially:

- the harness serves `out/` through a generated static server helper
- that helper is written inside the framework project itself as:
  - `.framework-test-static-server.js`

The helper is project-local on purpose so the safe stage can execute it without
depending on an out-of-tree script path.

### Host And Port Injection

The runtime launcher tries framework-specific argument shapes where needed,
including dedicated handling for:

- Docusaurus `start`
- `serve -l`
- Vite-style commands
- Next hostname/port flags
- generic host/port permutations

Each framework gets a deterministic port block derived from:

- `FRAMEWORK_TEST_PORT_BASE`
- `FRAMEWORK_TEST_PORT_BLOCK_SIZE`

### Success Criteria

A runtime stage only passes when the framework:

- starts a local process;
- responds on localhost;
- returns an HTTP response that looks like HTML.

The harness validates HTTP success, not just process liveness.

## Retry And Speed Behavior

The runtime stage is intentionally conservative about command shapes, but it now
fails faster on non-retryable startup errors.

### What Retries

The harness still retries alternate launch shapes on a port, and will move to
another port for errors that are plausibly port-related, such as:

- listener conflicts
- address binding failures
- connection resets/refusals
- startup timeouts

### What No Longer Retries Across Ports

If the failure is clearly not port-related, the harness stops trying the rest of
the port block and fails immediately for that framework/stage.

Examples include:

- module resolution failures
- runtime traps
- unsupported runtime behavior
- other deterministic startup crashes

This keeps the compatibility signal intact while avoiding slow repeated failures
such as trying ten ports for the same Wasmer trap.

### Current Concurrency Model

Current parallelism is intentionally limited to setup:

- `pnpm install` runs in parallel across frameworks
- runtime validation itself is still sequential within each stage

Cross-framework runtime parallelism is not part of this merge.

## Reset Behavior

`make framework-test-reset` now supports either all frameworks or one selected
framework.

The reset scope includes:

- selected framework `node_modules/`
- common generated framework outputs such as:
  - `dist`
  - `build`
  - `out`
  - `.next`
  - `.svelte-kit`
  - `.astro`
  - `.docusaurus`
  - `.cache`
  - `public/build`
  - `public/_gatsby`
  - `public/page-data`
- project-local `.framework-test-static-server.js`
- `.framework-test/`
- `build-edge/`
- repo-root `.pnpm-store/`

The reset path avoids touching source-controlled files. It also only removes an
untracked `public/` directory when that directory is not tracked in the
`wasmer-examples` submodule.

## Logs And Artifacts

Framework-test state lives under `.framework-test/`.

Important outputs include:

- `.framework-test/logs/<project>.pnpm-install.log`
- `.framework-test/logs/<project>.<stage>.build.log`
- `.framework-test/logs/<project>.<stage>.server.log`

These logs are the primary debugging artifact for framework startup failures and
stage-to-stage regressions.

## Current Limitations

The following are current intentional limits of the implementation:

- runtime validation is boot-and-serve validation, not deep application
  correctness
- runtime stages are sequential, not parallelized across frameworks
- safe mode is only attempted for an EdgeJS-like comparison runner
- the harness compares one baseline and one comparison runner, not an arbitrary
  N-way runtime matrix

## Verified Behavior

The implementation has been validated through `make framework-test` itself.

The focused proof run used:

- `make framework-test js-next-staticsite`

Observed behavior:

- `Node.js` passed
- `EdgeJS Native` passed
- `Wasmer + EdgeJS Safe` ran as a true third stage
- the safe-stage failure was surfaced as a real runtime compatibility error,
  not a harness-only path issue

This is the expected shape for the full-matrix command:

- `make framework-test`

## Merge Scope Summary

This feature is now fully scoped for merge as:

- a Makefile-driven framework compatibility harness
- automatic discovery of `wasmer-examples/js-*`
- per-stage runner injection
- a three-stage compatibility matrix
- clear per-stage and delta summaries
- reset support
- fail-fast retry behavior for non-port-related startup failures

Any future work after this merge should be treated as follow-up optimization,
not as missing scope from the current feature.
