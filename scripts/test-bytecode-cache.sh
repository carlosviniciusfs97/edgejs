#!/usr/bin/env bash
# End-to-end tests for bytecode sidecar caches (.v8b / .qjsb).
# Usage: EDGE_BIN=./build-edge/edge ./scripts/test-bytecode-cache.sh
set -u

EDGE_BIN="${EDGE_BIN:?usage: EDGE_BIN=<path-to-edge> $0}"
EDGE_BIN="$(cd "$(dirname "$EDGE_BIN")" && pwd)/$(basename "$EDGE_BIN")"

if [ ! -x "$EDGE_BIN" ]; then
  echo "error: EDGE_BIN is not executable: $EDGE_BIN" >&2
  exit 1
fi

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/edge-bytecode-cache-test.XXXXXX")"
# Resolve symlinked temp roots (macOS /var -> /private/var) so trace-output
# greps match the canonical paths the runtime reports.
WORKDIR="$(cd "$WORKDIR" && pwd -P)"
trap 'chmod -R u+w "$WORKDIR" 2>/dev/null; rm -rf "$WORKDIR"' EXIT

FAILURES=0
CURRENT=""
CURRENT_FAILURES=0

begin() {
  CURRENT="$1"
  CURRENT_FAILURES=$FAILURES
}

fail() {
  echo "FAIL: $CURRENT: $1" >&2
  FAILURES=$((FAILURES + 1))
}

pass() {
  if [ "$FAILURES" -eq "$CURRENT_FAILURES" ]; then
    echo "ok: $CURRENT"
  fi
}

sidecar_suffix() {
  # Detect which suffix this binary writes.
  local dir="$WORKDIR/suffix-probe"
  mkdir -p "$dir"
  echo "module.exports = 1;" > "$dir/probe.js"
  "$EDGE_BIN" --precompile "$dir" >/dev/null 2>&1
  if [ -f "$dir/probe.js.v8b" ]; then
    echo ".v8b"
  elif [ -f "$dir/probe.js.qjsb" ]; then
    echo ".qjsb"
  else
    echo ""
  fi
}

SUFFIX="$(sidecar_suffix)"
if [ -z "$SUFFIX" ]; then
  echo "error: could not determine sidecar suffix (precompile probe wrote nothing)" >&2
  exit 1
fi
echo "testing $EDGE_BIN (sidecar suffix: $SUFFIX)"

make_project() {
  local dir="$1"
  mkdir -p "$dir"
  cat > "$dir/lib.js" <<'EOF'
module.exports = { value: 21 };
EOF
  cat > "$dir/main.js" <<'EOF'
const lib = require('./lib.js');
console.log(lib.value * 2);
EOF
}

# --- precompile writes sidecars and the cached run output is identical -------
begin "precompile then run"
DIR="$WORKDIR/precompile"
make_project "$DIR"
baseline="$("$EDGE_BIN" --no-bytecode-cache "$DIR/main.js" 2>/dev/null)"
"$EDGE_BIN" --precompile "$DIR" >/dev/null 2>&1 || fail "precompile exited non-zero"
[ -f "$DIR/main.js$SUFFIX" ] || fail "missing main.js$SUFFIX"
[ -f "$DIR/lib.js$SUFFIX" ] || fail "missing lib.js$SUFFIX"
cached="$(EDGE_BYTECODE_CACHE_TRACE=1 "$EDGE_BIN" "$DIR/main.js" 2>"$WORKDIR/trace.txt")"
[ "$cached" = "$baseline" ] || fail "output mismatch: '$cached' vs '$baseline'"
grep -q "hit $DIR/main.js$SUFFIX" "$WORKDIR/trace.txt" || fail "expected a cache hit for main.js"
pass

# --- write-on-first-run -------------------------------------------------------
begin "write-on-first-run then consume"
DIR="$WORKDIR/first-run"
make_project "$DIR"
out1="$(EDGE_BYTECODE_CACHE_TRACE=1 "$EDGE_BIN" "$DIR/main.js" 2>"$WORKDIR/trace1.txt")"
[ "$out1" = "42" ] || fail "first run output: $out1"
grep -q "write $DIR/main.js$SUFFIX" "$WORKDIR/trace1.txt" || fail "first run did not write sidecar"
out2="$(EDGE_BYTECODE_CACHE_TRACE=1 "$EDGE_BIN" "$DIR/main.js" 2>"$WORKDIR/trace2.txt")"
[ "$out2" = "42" ] || fail "second run output: $out2"
grep -q "hit $DIR/main.js$SUFFIX" "$WORKDIR/trace2.txt" || fail "second run did not consume sidecar"
pass

# --- corrupted sidecar falls back and is rewritten ----------------------------
begin "corrupted sidecar falls back"
DIR="$WORKDIR/corrupt"
make_project "$DIR"
"$EDGE_BIN" --precompile "$DIR" >/dev/null 2>&1
printf 'garbage' > "$DIR/main.js$SUFFIX"
out="$("$EDGE_BIN" "$DIR/main.js" 2>/dev/null)"
[ "$out" = "42" ] || fail "corrupted sidecar broke execution: $out"
size="$(wc -c < "$DIR/main.js$SUFFIX")"
[ "$size" -gt 64 ] || fail "corrupted sidecar was not rewritten (size=$size)"
pass

# --- stale sidecar (source edited) falls back ---------------------------------
begin "stale sidecar falls back"
DIR="$WORKDIR/stale"
make_project "$DIR"
"$EDGE_BIN" --precompile "$DIR" >/dev/null 2>&1
echo "console.log('changed');" > "$DIR/main.js"
out="$("$EDGE_BIN" "$DIR/main.js" 2>/dev/null)"
[ "$out" = "changed" ] || fail "stale sidecar served old code: $out"
pass

# --- opt-outs ------------------------------------------------------------------
begin "--no-bytecode-cache writes and reads nothing"
DIR="$WORKDIR/optout-flag"
make_project "$DIR"
"$EDGE_BIN" --no-bytecode-cache "$DIR/main.js" >/dev/null 2>&1
[ ! -f "$DIR/main.js$SUFFIX" ] || fail "sidecar written despite --no-bytecode-cache"
pass

begin "EDGE_BYTECODE_CACHE=0 writes and reads nothing"
DIR="$WORKDIR/optout-env"
make_project "$DIR"
EDGE_BYTECODE_CACHE=0 "$EDGE_BIN" "$DIR/main.js" >/dev/null 2>&1
[ ! -f "$DIR/main.js$SUFFIX" ] || fail "sidecar written despite EDGE_BYTECODE_CACHE=0"
pass

# --- read-only tree ------------------------------------------------------------
begin "read-only directory still runs"
DIR="$WORKDIR/readonly"
make_project "$DIR"
chmod -R a-w "$DIR"
out="$("$EDGE_BIN" "$DIR/main.js" 2>/dev/null)"
chmod -R u+w "$DIR"
[ "$out" = "42" ] || fail "read-only tree broke execution: $out"
[ ! -f "$DIR/main.js$SUFFIX" ] || fail "sidecar appeared in read-only tree"
pass

# --- shebang and BOM -----------------------------------------------------------
begin "shebang and BOM sources round-trip"
DIR="$WORKDIR/encodings"
mkdir -p "$DIR"
printf '#!/usr/bin/env node\nconsole.log("shebang");\n' > "$DIR/shebang.js"
printf '\xef\xbb\xbfconsole.log("bom");\n' > "$DIR/bom.js"
out_a1="$("$EDGE_BIN" "$DIR/shebang.js" 2>/dev/null)"
out_a2="$("$EDGE_BIN" "$DIR/shebang.js" 2>/dev/null)"
[ "$out_a1" = "shebang" ] && [ "$out_a2" = "shebang" ] || fail "shebang: '$out_a1' / '$out_a2'"
out_b1="$("$EDGE_BIN" "$DIR/bom.js" 2>/dev/null)"
out_b2="$("$EDGE_BIN" "$DIR/bom.js" 2>/dev/null)"
[ "$out_b1" = "bom" ] && [ "$out_b2" = "bom" ] || fail "bom: '$out_b1' / '$out_b2'"
pass

# --- package type rules ---------------------------------------------------------
begin "precompile respects package.json type=module"
DIR="$WORKDIR/typemodule"
mkdir -p "$DIR"
echo '{ "type": "module" }' > "$DIR/package.json"
echo 'export const x = 1;' > "$DIR/esm.js"
echo 'module.exports = 1;' > "$DIR/legacy.cjs"
"$EDGE_BIN" --precompile "$DIR" >/dev/null 2>&1 || fail "precompile exited non-zero"
[ -f "$DIR/esm.js$SUFFIX" ] || fail ".js in type=module scope must get an ESM-shape sidecar"
[ -f "$DIR/legacy.cjs$SUFFIX" ] || fail ".cjs must be precompiled regardless of scope"
pass

# --- ESM sidecars -----------------------------------------------------------------
begin "esm write-on-first-run then consume"
DIR="$WORKDIR/esm-first-run"
mkdir -p "$DIR"
cat > "$DIR/lib.mjs" <<'EOF'
export const value = 21;
EOF
cat > "$DIR/main.mjs" <<'EOF'
import { value } from './lib.mjs';
console.log(value * 2);
EOF
out1="$(EDGE_BYTECODE_CACHE_TRACE=1 "$EDGE_BIN" "$DIR/main.mjs" 2>"$WORKDIR/etrace1.txt")"
[ "$out1" = "42" ] || fail "first run output: $out1"
grep -q "write $DIR/main.mjs$SUFFIX" "$WORKDIR/etrace1.txt" || fail "first run did not write esm sidecar"
out2="$(EDGE_BYTECODE_CACHE_TRACE=1 "$EDGE_BIN" "$DIR/main.mjs" 2>"$WORKDIR/etrace2.txt")"
[ "$out2" = "42" ] || fail "second run output: $out2"
grep -q "hit $DIR/main.mjs$SUFFIX" "$WORKDIR/etrace2.txt" || fail "second run did not consume esm sidecar"
grep -q "hit $DIR/lib.mjs$SUFFIX" "$WORKDIR/etrace2.txt" || fail "imported module did not consume esm sidecar"
pass

begin "esm import chain fully cached"
DIR="$WORKDIR/esm-chain"
mkdir -p "$DIR"
echo 'export const c = 7;' > "$DIR/c.mjs"
printf "import { c } from './c.mjs';\nexport const b = c * 2;\n" > "$DIR/b.mjs"
printf "import { b } from './b.mjs';\nconsole.log(b * 3);\n" > "$DIR/a.mjs"
"$EDGE_BIN" --precompile "$DIR" >/dev/null 2>&1 || fail "precompile exited non-zero"
out="$(EDGE_BYTECODE_CACHE_TRACE=1 "$EDGE_BIN" "$DIR/a.mjs" 2>"$WORKDIR/chain.txt")"
[ "$out" = "42" ] || fail "chain output: $out"
hits="$(grep -c "hit " "$WORKDIR/chain.txt")"
[ "$hits" -eq 3 ] || fail "expected 3 cache hits, got $hits"
pass

begin "dynamic import and import.meta from cached modules"
DIR="$WORKDIR/esm-dynamic"
mkdir -p "$DIR"
echo 'export const value = 5;' > "$DIR/lib.mjs"
cat > "$DIR/main.mjs" <<'EOF'
const mod = await import('./lib.mjs');
console.log(mod.value, import.meta.url.endsWith('main.mjs'));
EOF
"$EDGE_BIN" "$DIR/main.mjs" >/dev/null 2>&1
out="$("$EDGE_BIN" "$DIR/main.mjs" 2>/dev/null)"
[ "$out" = "5 true" ] || fail "cached dynamic import / import.meta: $out"
pass

# Regression: import.meta inside an IMPORTED cached module. The importer's
# bytecode read used to register stub modules under the dependency's real URL,
# shadowing the real module in quickjs's name-based import.meta lookup.
begin "import.meta in imported cached module"
DIR="$WORKDIR/esm-meta-dep"
mkdir -p "$DIR"
cat > "$DIR/dep.mjs" <<'EOF'
export const fromDep = import.meta.url;
EOF
cat > "$DIR/main.mjs" <<'EOF'
import { fromDep } from './dep.mjs';
console.log(typeof fromDep, String(fromDep).endsWith('dep.mjs'));
EOF
"$EDGE_BIN" "$DIR/main.mjs" >/dev/null 2>&1
out="$("$EDGE_BIN" "$DIR/main.mjs" 2>/dev/null)"
[ "$out" = "string true" ] || fail "import.meta in imported cached module: $out"
pass

begin "top-level await module cached"
DIR="$WORKDIR/esm-tla"
mkdir -p "$DIR"
cat > "$DIR/main.mjs" <<'EOF'
const v = await Promise.resolve('tla-ok');
console.log(v);
EOF
"$EDGE_BIN" "$DIR/main.mjs" >/dev/null 2>&1
out="$("$EDGE_BIN" "$DIR/main.mjs" 2>/dev/null)"
[ "$out" = "tla-ok" ] || fail "cached TLA module: $out"
pass

begin "corrupted esm sidecar falls back"
DIR="$WORKDIR/esm-corrupt"
mkdir -p "$DIR"
echo 'console.log("esm-fine");' > "$DIR/main.mjs"
"$EDGE_BIN" --precompile "$DIR" >/dev/null 2>&1
printf 'garbage' > "$DIR/main.mjs$SUFFIX"
out="$("$EDGE_BIN" "$DIR/main.mjs" 2>/dev/null)"
[ "$out" = "esm-fine" ] || fail "corrupted esm sidecar broke execution: $out"
pass

begin "stale esm sidecar falls back"
DIR="$WORKDIR/esm-stale"
mkdir -p "$DIR"
echo 'console.log("esm-old");' > "$DIR/main.mjs"
"$EDGE_BIN" --precompile "$DIR" >/dev/null 2>&1
echo 'console.log("esm-new");' > "$DIR/main.mjs"
out="$("$EDGE_BIN" "$DIR/main.mjs" 2>/dev/null)"
[ "$out" = "esm-new" ] || fail "stale esm sidecar served old code: $out"
pass

begin "detect-module .js precompiles as ESM and runs cached"
DIR="$WORKDIR/esm-detect"
mkdir -p "$DIR"
echo 'export default 1; console.log("detected");' > "$DIR/ambiguous.js"
"$EDGE_BIN" --precompile "$DIR" >/dev/null 2>&1 || fail "precompile exited non-zero"
[ -f "$DIR/ambiguous.js$SUFFIX" ] || fail "ESM-syntax .js should get an ESM-shape sidecar"
out="$("$EDGE_BIN" "$DIR/ambiguous.js" 2>/dev/null)"
[ "$out" = "detected" ] || fail "detect-module run with sidecar: $out"
pass

# --- vm cachedData round-trips ----------------------------------------------------
begin "vm.Script and vm.compileFunction cachedData round-trip"
DIR="$WORKDIR/vm-cache"
mkdir -p "$DIR"
cat > "$DIR/vm-test.cjs" <<'EOF'
const vm = require('node:vm');
const script = new vm.Script('1 + 41', { produceCachedData: true });
if (!script.cachedData || script.cachedData.length === 0) throw new Error('no script cachedData');
const script2 = new vm.Script('1 + 41', { cachedData: script.cachedData });
if (script2.cachedDataRejected !== false) throw new Error('script cachedData rejected: ' + script2.cachedDataRejected);
if (script2.runInThisContext() !== 42) throw new Error('script2 result');
const fn = vm.compileFunction('return a + b;', ['a', 'b'], { produceCachedData: true, filename: 'fn-test.js' });
if (!fn.cachedData || fn.cachedData.length === 0) throw new Error('no fn cachedData');
const fn2 = vm.compileFunction('return a + b;', ['a', 'b'], { cachedData: fn.cachedData, filename: 'fn-test.js' });
if (fn2(40, 2) !== 42) throw new Error('fn2 result');
console.log('vm-ok');
EOF
out="$("$EDGE_BIN" --no-bytecode-cache "$DIR/vm-test.cjs" 2>&1)"
[ "$out" = "vm-ok" ] || fail "vm cachedData round-trip: $out"
pass

begin "vm.SourceTextModule cachedData round-trip"
DIR="$WORKDIR/vm-module"
mkdir -p "$DIR"
cat > "$DIR/vm-module-test.cjs" <<'EOF'
const vm = require('node:vm');
async function main() {
  const m1 = new vm.SourceTextModule('export const x = 42;');
  const cached = m1.createCachedData();
  if (!cached || cached.length === 0) throw new Error('no module cachedData');
  const m2 = new vm.SourceTextModule('export const x = 42;', { cachedData: cached });
  await m2.link(() => { throw new Error('no links expected'); });
  await m2.evaluate();
  if (m2.namespace.x !== 42) throw new Error('module namespace');
  console.log('vm-module-ok');
}
main().catch((err) => { console.error(err.message); process.exit(1); });
EOF
out="$("$EDGE_BIN" --no-bytecode-cache --no-warnings --experimental-vm-modules "$DIR/vm-module-test.cjs" 2>/dev/null)"
[ "$out" = "vm-module-ok" ] || fail "vm.SourceTextModule cachedData round-trip: $out"
pass

# --- --check stays clean ---------------------------------------------------------
begin "--check writes no sidecars"
DIR="$WORKDIR/checkmode"
make_project "$DIR"
"$EDGE_BIN" --check "$DIR/main.js" >/dev/null 2>&1
[ ! -f "$DIR/main.js$SUFFIX" ] || fail "--check wrote a sidecar"
pass

# --- conflicts -------------------------------------------------------------------
begin "--precompile conflict validation"
"$EDGE_BIN" --precompile >/dev/null 2>&1 && fail "--precompile with no paths should fail"
"$EDGE_BIN" --precompile --check x.js >/dev/null 2>&1 && fail "--precompile --check should fail"
"$EDGE_BIN" --precompile --no-bytecode-cache x >/dev/null 2>&1 && fail "--precompile --no-bytecode-cache should fail"
pass

echo
if [ "$FAILURES" -gt 0 ]; then
  echo "$FAILURES failure(s)" >&2
  exit 1
fi
echo "all bytecode-cache tests passed"
