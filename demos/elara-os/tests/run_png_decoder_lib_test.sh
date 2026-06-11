#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
PROJECT="$ROOT/demos/elara-os"
BUILD="$PROJECT/build/tests"
HARNESS="$BUILD/png_decoder_lib_check"

mkdir -p "$BUILD"

ELARA_OS_SKIP_DISK_INSTALL=1 "$PROJECT/build_epa.sh" --target png_decoder_lib

gcc -std=gnu11 \
  -I"$ROOT/libElaraParallelAssembly" \
  -I"$ROOT/libElaraParallelAssembly/src" \
  "$PROJECT/tests/png_decoder_lib_check.c" \
  "$ROOT/libElaraParallelAssembly/build/libelaraparallelassembly.a" \
  -lpthread \
  -lm \
  -o "$HARNESS"

"$HARNESS"
