#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BIN="$BUILD_DIR/bin/elara-unit-tests"
LOG_DIR="$ROOT_DIR/valgrind-logs"
STAMP="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$LOG_DIR"

if ! command -v valgrind >/dev/null 2>&1; then
  echo "valgrind is not installed" >&2
  exit 1
fi

if [ ! -x "$BIN" ]; then
  echo "Building ElaraUnitTests first..."
  make -C "$ROOT_DIR" ElaraUnitTests ROOT_BUILD_DIR="$BUILD_DIR"
  make -C "$ROOT_DIR/ElaraUnitTests" install ROOT_BUILD_DIR="$BUILD_DIR"
fi

run_case() {
  local name="$1"
  shift
  local log_file="$LOG_DIR/${STAMP}-${name}.log"

  echo "Running $name"
  if ! valgrind \
      --tool=memcheck \
      --leak-check=full \
      --show-leak-kinds=all \
      --track-origins=yes \
      --num-callers=32 \
      --error-exitcode=101 \
      --log-file="$log_file" \
      "$BIN" "$@"; then
    echo "Failure logged to $log_file" >&2
    return 1
  fi

  echo "Log written to $log_file"
}

run_case unit-tests
run_case stress-memory stress-memory
