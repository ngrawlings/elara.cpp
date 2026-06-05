#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD="$ROOT/demos/e_examples/build/tests"
HARNESS="$BUILD/vm_state_ingress_check"

mkdir -p "$BUILD"

gcc -std=gnu11 \
  -I"$ROOT/libElaraParallelAssembly" \
  -I"$ROOT/libElaraParallelAssembly/src" \
  "$ROOT/demos/e_examples/tests/vm_state_ingress_check.c" \
  "$ROOT/libElaraParallelAssembly/build/libelaraparallelassembly.a" \
  -lpthread \
  -o "$HARNESS"

"$HARNESS"
