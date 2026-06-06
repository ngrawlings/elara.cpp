#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E2EPA="$ROOT/libElaraParallelAssembly/build/e/e2epa"
BUILD="$ROOT/demos/e_examples/build/tests"
HARNESS="$BUILD/vm_state_ingress_check"

mkdir -p "$BUILD"

"$E2EPA" "$ROOT/demos/e_examples/tests/vm_state_builtins/epa/entry.e" "$BUILD/vm_state_builtins_entry.epaasm"
grep -q "VM_STATE 0" "$BUILD/vm_state_builtins_entry.epaasm"
grep -q "VM_STATE 1" "$BUILD/vm_state_builtins_entry.epaasm"
grep -q "VM_STATE 2" "$BUILD/vm_state_builtins_entry.epaasm"
grep -q "VM_STATE 3" "$BUILD/vm_state_builtins_entry.epaasm"

gcc -std=gnu11 \
  -I"$ROOT/libElaraParallelAssembly" \
  -I"$ROOT/libElaraParallelAssembly/src" \
  "$ROOT/demos/e_examples/tests/vm_state_ingress_check.c" \
  "$ROOT/libElaraParallelAssembly/build/libelaraparallelassembly.a" \
  -lpthread \
  -o "$HARNESS"

"$HARNESS"
