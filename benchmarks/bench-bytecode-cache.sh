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
GEN_DIR="benchmarks/workloads/bytecode-cache/gen"

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

echo "=== Generating workload ==="
"$NODE_BIN" benchmarks/generate-bytecode-workload.mjs

# Identify the engine by the sidecar suffix it writes.
"$EDGE_BIN" --precompile "$GEN_DIR" >/dev/null
if compgen -G "$GEN_DIR/*.v8b" >/dev/null; then
  LABEL="v8"
  SUFFIX=".v8b"
elif compgen -G "$GEN_DIR/*.qjsb" >/dev/null; then
  LABEL="quickjs"
  SUFFIX=".qjsb"
else
  echo "error: precompile produced no sidecars in $GEN_DIR" >&2
  exit 1
fi

CLEAN_SIDECARS="find $GEN_DIR -name '*$SUFFIX' -delete"
PRECOMPILE="$EDGE_BIN --precompile $GEN_DIR >/dev/null"

JSON_OUT="$RESULTS_DIR/bytecode-cache-$LABEL.json"
CSV_OUT="$RESULTS_DIR/bytecode-cache-$LABEL.csv"
MD_OUT="$RESULTS_DIR/bytecode-cache-$LABEL.md"

echo
echo "=== Benchmarking ($LABEL, warmup=$WARMUP, runs=$RUNS) ==="
hyperfine \
  --warmup "$WARMUP" \
  --runs "$RUNS" \
  --export-json "$JSON_OUT" \
  --export-csv "$CSV_OUT" \
  --export-markdown "$MD_OUT" \
  --prepare "$CLEAN_SIDECARS" \
  --command-name "cold (no cache)" "$EDGE_BIN --no-bytecode-cache $GEN_DIR/main.js" \
  --prepare "$CLEAN_SIDECARS" \
  --command-name "first run (writes sidecars)" "$EDGE_BIN $GEN_DIR/main.js" \
  --prepare "$PRECOMPILE" \
  --command-name "warm (precompiled sidecars)" "$EDGE_BIN $GEN_DIR/main.js"

"$NODE_BIN" - "$JSON_OUT" "$LABEL" <<'EOF'
const { readFileSync } = require('node:fs');
const [jsonPath, label] = process.argv.slice(2);
const results = JSON.parse(readFileSync(jsonPath, 'utf8')).results;
const byName = Object.fromEntries(results.map((r) => [r.command, r]));
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
echo "  $JSON_OUT"
echo "  $CSV_OUT"
echo "  $MD_OUT"
