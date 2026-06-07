#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
E2EPA="$ROOT/libElaraParallelAssembly/build/e/e2epa"
BUILD="$ROOT/demos/e_examples/build/tests"
HARNESS="$BUILD/privilege_acl_check"

mkdir -p "$BUILD"

"$E2EPA" "$ROOT/demos/e_examples/tests/privileged_workers/epa/entry.e" "$BUILD/privileged_workers_entry.epaasm"
"$E2EPA" "$ROOT/demos/e_examples/tests/dynamic_whitelist/epa/entry.e" "$BUILD/dynamic_whitelist_entry.epaasm"
"$E2EPA" "$ROOT/demos/e_examples/tests/dynamic_whitelist/epa/target.e" "$BUILD/dynamic_whitelist_target.epaasm"
"$E2EPA" "$ROOT/demos/e_examples/tests/dynamic_whitelist/epa/app.e" "$BUILD/dynamic_whitelist_app.epaasm"

grep -q "ENTRY_PRIVILEGE 1 100" "$BUILD/privileged_workers_entry.epaasm"
grep -q "PRIVILEGE_LOCK" "$BUILD/privileged_workers_entry.epaasm"
grep -q "ENTRY_PRIVILEGE 1 100" "$BUILD/dynamic_whitelist_entry.epaasm"
grep -q "PRIVILEGE_LOCK" "$BUILD/dynamic_whitelist_entry.epaasm"
grep -q "ACL 1 1" "$BUILD/dynamic_whitelist_entry.epaasm"
grep -q "ACL 2 1" "$BUILD/dynamic_whitelist_entry.epaasm"
grep -q "ACL 3 0" "$BUILD/dynamic_whitelist_entry.epaasm"

gcc -std=gnu11 \
  -I"$ROOT/libElaraParallelAssembly" \
  -I"$ROOT/libElaraParallelAssembly/src" \
  "$ROOT/demos/e_examples/tests/privilege_acl_check.c" \
  "$ROOT/libElaraParallelAssembly/build/libelaraparallelassembly.a" \
  -lm -lpthread \
  -o "$HARNESS"

"$HARNESS"
