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
[ ! -f "$DIR/esm.js$SUFFIX" ] || fail ".js in type=module scope must be skipped"
[ -f "$DIR/legacy.cjs$SUFFIX" ] || fail ".cjs must be precompiled regardless of scope"
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
