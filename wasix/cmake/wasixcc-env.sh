#!/usr/bin/env sh

export WASIXCC_WASM_EXCEPTIONS=exnref
export WASIXCC_RUN_WASM_OPT=no

WASIXCC_DRIVER="${1:-wasixcc}"
export WASIXCC_SYSROOT="${WASIXCC_SYSROOT:-$(wasixccenv print-sysroot)}"
