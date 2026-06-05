#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E2EPA="$ROOT/libElaraParallelAssembly/build/e/e2epa"
E2EPABIN="$ROOT/libElaraParallelAssembly/build/e/e2epabin"
BUILD="$ROOT/demos/e_examples/build/tests"
HARNESS="$BUILD/process_cluster_check"

mkdir -p "$BUILD"

"$E2EPA" "$ROOT/demos/e_examples/tests/privileged_workers/epa/entry.e" "$BUILD/process_cluster_authority.epaasm"
"$E2EPABIN" \
  --out "$BUILD/process_cluster_app.epa.bin" \
  "$ROOT/demos/e_examples/tests/process_cluster/epa/entry.e" \
  "$ROOT/demos/e_examples/tests/process_cluster/epa/app_helper.e"

gcc -std=gnu11 \
  -I"$ROOT/libElaraParallelAssembly" \
  -I"$ROOT/libElaraParallelAssembly/src" \
  "$ROOT/demos/e_examples/tests/process_cluster_check.c" \
  "$ROOT/libElaraParallelAssembly/build/libelaraparallelassembly.a" \
  -lpthread \
  -o "$HARNESS"

"$HARNESS"
