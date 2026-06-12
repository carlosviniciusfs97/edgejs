# Bytecode Precompilation (Sidecar Code Caches)

## Goal

Cut process startup time by skipping JS parse/compile for CommonJS modules.
Both engines serialize compiled code — V8 via `ScriptCompiler` code caches,
QuickJS (quickjs-ng) via `JS_WriteObject`/`JS_ReadObject` bytecode — and the
serialized form is stored as a **sidecar file next to the source**:

```
app.js          the source (unchanged, still authoritative)
app.js.v8b      V8 code cache        (written by the bundled-v8 build)
app.js.qjsb     QuickJS bytecode     (written by the quickjs build)
```

Both sidecars can coexist; each binary only reads its own suffix.

## Behavior

- **Consume (default-on):** when the CJS loader compiles a file and a valid
  sidecar exists, the engine consumes it instead of parsing the source.
  Invalid, stale, or corrupted sidecars are silently ignored (normal compile).
- **Write-on-first-run (default-on):** when no valid sidecar exists, the
  runtime produces the engine cache during the normal compile and writes the
  sidecar atomically (temp file + rename). Failures (read-only filesystem,
  permissions) are silent.
- **Explicit precompile:** `edge --precompile <file|dir>...` walks the paths,
  compiles every eligible CJS file (`.cjs` always; `.js` unless its nearest
  `package.json` declares `"type": "module"`, `node_modules` included) and
  writes sidecars **without executing any module body**. ESM-syntax files are
  skipped, not failed.
- **Opt-outs:** `--no-bytecode-cache` (CLI) or `EDGE_BYTECODE_CACHE=0` (env)
  disable both reads and writes. `--check` disables the cache implicitly.
  `--precompile` conflicts with `--check/--eval/--test/--watch/--interactive/
  --run/--no-bytecode-cache` (exit 9).
- **Tracing:** `EDGE_BYTECODE_CACHE_TRACE=1` prints `hit/miss/write/remove`
  lines to stderr (used by tests).

## Sidecar format

Fixed 64-byte little-endian header + engine tag + opaque engine payload
(`src/edge_bytecode_cache.h`):

| field | notes |
|---|---|
| magic `EDGEJSBC` | |
| format_version u32 | bump when the compile shape changes (wrapper text, params, shebang handling) |
| flags u32 | bit0 = CJS function-compile shape v1 |
| engine_tag_len u32 | |
| source_len + source_hash u64 | FNV-1a-64 over the exact source string handed to the compiler (BOM/TS-stripping/shebang concerns vanish) |
| filename_hash u64 | QuickJS writes + enforces it (its bytecode embeds the compile-time path); V8 writes 0 and stays relocatable |
| payload_len + payload_hash u64 | corruption guard |
| engine tag | `v8-<EDGE_EMBEDDED_V8_VERSION>` / `qjs-ng-<EDGE_EMBEDDED_QUICKJS_VERSION>` |

Validation order: magic → version → flags → tag → source hash → filename hash
→ payload hash. Any miss = "no cache". V8 additionally self-validates
(`CachedData::rejected`); a rejected sidecar is deleted so the next run
rewrites it instead of re-rejecting forever.

## Implementation map

No JS under `lib/` was touched — the feature is invisible to the Node-ported
loader. All logic is C++:

- `src/edge_bytecode_cache.{h,cc}` — container format, validation, hashing,
  atomic writes, enable state. Engine identity comes from compile defines
  (`EDGE_EMBEDDED_V8_VERSION` / `EDGE_EMBEDDED_QUICKJS_VERSION`, the latter
  parsed from `quickjs.h` in the root `CMakeLists.txt`).
- `src/edge_module_loader.cc` `ContextifyCompileFunctionForCJSLoaderCallback`
  — the single runtime hook. The JS loader's `wrapSafe` already calls this
  binding for every user CJS module; it now reads the sidecar before the
  compile, passes the payload as cached data into the (pre-existing)
  `unofficial_napi_contextify_compile_function` cache parameters, and
  writes/removes the sidecar afterwards. The JS-visible result shape is
  unchanged.
- `napi/v8/src/unofficial_napi_contextify.cc` — already supported cached-data
  consume/produce. Only change: compile with `kEagerCompile` when producing a
  cache so inner functions are serialized too (matches Node's compile cache).
- `napi/quickjs/src/internal/napi_contextify.cc` — `compile_cjs_function` now
  consumes (`JS_ReadObject` + `JS_EvalFunction`; exception ⇒ rejected ⇒ text
  compile) and produces (`JS_WriteObject` on the `COMPILE_ONLY` result), and
  `compile_function` reports `cachedData`/`cachedDataProduced`/
  `cachedDataRejected` exactly like the V8 provider.
- `src/edge_precompile.{h,cc}` — the `--precompile` driver (file walk, package
  `type` scope resolution with a per-directory cache, compile-only, summary,
  exit 1 if any file failed).
- `src/edge_cli.cc` — `CliMode::kPrecompile`, flag registration, conflicts,
  `SetEnabledFromCli(false)` for `--no-bytecode-cache`/`--check`.

## Tests

- `tests/runners/test_7_bytecode_cache_phase05.cc` (gtest, V8 lane): header
  encode/decode round-trip + every corruption mode, plus spawned-binary e2e
  (write-on-first-run, precompile-without-executing, corrupted/stale fallback,
  `--check` cleanliness, conflict exits, ESM skip).
- `scripts/test-bytecode-cache.sh` (both engines): `make test-bytecode-cache`
  / `make test-bytecode-cache-quickjs`. Covers the same scenarios plus
  read-only trees, shebang/BOM sources, `type: module` scoping and env-var
  opt-out, asserting via `EDGE_BYTECODE_CACHE_TRACE`.

## Benchmark

`benchmarks/bench-bytecode-cache.sh` (hyperfine; `EDGE_BIN` selects engine).
`benchmarks/generate-bytecode-workload.mjs` generates a deterministic ~3.4 MB
CJS app (150 modules + 1.5 MB vendor bundle). Lanes: cold
(`--no-bytecode-cache`), first run (compile + write), warm (precompiled
sidecars). Results land in `benchmarks/results/bytecode-cache-<engine>.*` with
a printed speedup summary.

Measured 2026-06-11 (Apple Silicon, warmup 3, 20 runs; empty-startup baseline
37 ms V8 / 55 ms QuickJS):

| engine | cold | first run (writes) | warm | full-startup speedup | module-load portion |
|---|---|---|---|---|---|
| V8 | 92.4 ms ± 2.5 | 137.6 ms ± 6.9 | 74.4 ms ± 2.7 | **1.24×** | 55 ms → 37 ms |
| QuickJS | 184.9 ms ± 4.7 | 248.4 ms ± 7.7 | 143.4 ms ± 3.4 | **1.29×** | 130 ms → 89 ms |

All 152 modules report `hit` under `EDGE_BYTECODE_CACHE_TRACE=1` on both
engines (no rejects). The fixed cost that remains is builtin bootstrap —
the phase-3 item below; gains grow with app size since the cached portion
scales with user code.

## Known limitations / phase 2+

- **ESM (`.mjs`, `type: module`) is not cached yet.** V8 plumbing exists
  (`unofficial_napi_module_wrap_create_source_text` accepts cached data and
  `unofficial_napi_module_wrap_create_cached_data` is implemented); QuickJS
  needs the stub in `napi/quickjs/src/internal/napi_module_wrap.cc`
  (`create_cached_data`) implemented with `JS_WRITE_OBJ_BYTECODE`, plus a
  consume path. The runtime hook belongs in the module_wrap binding layer,
  same no-`lib/` approach.
- **Builtins are not cached.** Embedded builtins compile on every boot via
  `EvaluateJsModule`; a build-time bytecode catalog would speed up *empty*
  startup and is likely the biggest absolute win. Separate initiative.
- QuickJS sidecars are keyed to the absolute file path (stack-trace fidelity);
  relocated trees recompile once and rewrite. V8 sidecars are relocatable.
- Node's `module.enableCompileCache()` stubs remain disabled; sidecars
  supersede them.
- `vm.compileFunction` with `produceCachedData` now compiles eagerly under V8
  (better cache coverage, marginally slower compile) — intentional deviation.
- The Node test harness lanes (`make test-only` / `test-quickjs-only`) run with
  `EDGE_BYTECODE_CACHE=0` so write-on-first-run does not spray sidecars into
  `test/fixtures/`. Both lanes were verified green with the cache enabled too;
  dedicated cache coverage lives in the suites above.
