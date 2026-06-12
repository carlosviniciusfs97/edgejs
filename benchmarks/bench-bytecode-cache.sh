#!/usr/bin/env bash
# Startup benchmark for bytecode sidecar caches: cold compile vs precompiled.
#
# Usage:
#   EDGE_BIN=./build-edge/edge ./benchmarks/bench-bytecode-cache.sh
#   EDGE_BIN=./build-edge-quickjs-cli/edge ./benchmarks/bench-bytecode-cache.sh
set -euo pipefail

EDGE_BIN="${EDGE_BIN:-./build-edge/edge}"
NODE_BIN="${NODE_BIN:-node}"
WARMUP="${WARMUP:-3}"
RUNS="${RUNS:-20}"
RESULTS_DIR="${RESULTS_DIR:-benchmarks/results}"
GEN_BASE="benchmarks/workloads/bytecode-cache"

command -v hyperfine >/dev/null 2>&1 || {
  echo "Missing dependency: hyperfine"
  echo "Install it first, then re-run this script."
  exit 1
}

[[ -x "$EDGE_BIN" ]] || {
  echo "Missing edge binary: $EDGE_BIN"
  echo "Build Edge first with: make build (or make build-edge-quickjs-cli)"
  exit 1
}

command -v "$NODE_BIN" >/dev/null 2>&1 || {
  echo "Missing node binary: $NODE_BIN (used to generate the workload)"
  exit 1
}

mkdir -p "$RESULTS_DIR"

echo "=== Generating workloads ==="
"$NODE_BIN" benchmarks/generate-bytecode-workload.mjs

# Identify the engine by the sidecar suffix it writes.
"$EDGE_BIN" --precompile "$GEN_BASE/gen" >/dev/null
if compgen -G "$GEN_BASE/gen/*.v8b" >/dev/null; then
  LABEL="v8"
  SUFFIX=".v8b"
elif compgen -G "$GEN_BASE/gen/*.qjsb" >/dev/null; then
  LABEL="quickjs"
  SUFFIX=".qjsb"
else
  echo "error: precompile produced no sidecars in $GEN_BASE/gen" >&2
  exit 1
fi

run_lane() {
  local lane="$1"   # cjs | esm
  local gen_dir="$2"
  local main="$3"

  local clean_sidecars="find $gen_dir -name '*$SUFFIX' -delete"
  local precompile="$EDGE_BIN --precompile $gen_dir >/dev/null"
  local json_out="$RESULTS_DIR/bytecode-cache-$LABEL-$lane.json"
  local csv_out="$RESULTS_DIR/bytecode-cache-$LABEL-$lane.csv"
  local md_out="$RESULTS_DIR/bytecode-cache-$LABEL-$lane.md"

  echo
  echo "=== Benchmarking ($LABEL, $lane, warmup=$WARMUP, runs=$RUNS) ==="
  hyperfine \
    --warmup "$WARMUP" \
    --runs "$RUNS" \
    --export-json "$json_out" \
    --export-csv "$csv_out" \
    --export-markdown "$md_out" \
    --prepare "$clean_sidecars" \
    --command-name "cold (no cache)" "$EDGE_BIN --no-bytecode-cache $gen_dir/$main" \
    --prepare "$clean_sidecars" \
    --command-name "first run (writes sidecars)" "$EDGE_BIN $gen_dir/$main" \
    --prepare "$precompile" \
    --command-name "warm (precompiled sidecars)" "$EDGE_BIN $gen_dir/$main"

  "$NODE_BIN" - "$json_out" "$LABEL/$lane" <<'EOF'
const { readFileSync } = require('node:fs');
const [jsonPath, label] = process.argv.slice(2);
const results = JSON.parse(readFileSync(jsonPath, 'utf8')).results;
const cold = results.find((r) => r.command.startsWith('cold'));
const firstRun = results.find((r) => r.command.startsWith('first run'));
const warm = results.find((r) => r.command.startsWith('warm'));
const ms = (s) => (s * 1000).toFixed(1);
console.log('');
console.log(`=== Bytecode cache startup summary (${label}) ===`);
console.log(`cold (compile from source):   ${ms(cold.mean)} ms ± ${ms(cold.stddev)} ms`);
console.log(`first run (compile + write):  ${ms(firstRun.mean)} ms ± ${ms(firstRun.stddev)} ms`);
console.log(`warm (load sidecars):         ${ms(warm.mean)} ms ± ${ms(warm.stddev)} ms`);
console.log('');
console.log(`startup speedup (cold -> warm): ${(cold.mean / warm.mean).toFixed(2)}x`);
console.log(`time saved per start:           ${ms(cold.mean - warm.mean)} ms`);
console.log(`first-run write overhead:       ${ms(firstRun.mean - cold.mean)} ms`);
EOF

  echo
  echo "Exported:"
  echo "  $json_out"
  echo "  $csv_out"
  echo "  $md_out"
}

run_lane "cjs" "$GEN_BASE/gen" "main.js"
run_lane "esm" "$GEN_BASE/gen-esm" "main.mjs"
