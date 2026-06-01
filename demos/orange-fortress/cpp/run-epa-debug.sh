#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_ROOT="$(cd "${ROOT_DIR}/.." && pwd)"
PID_FILE="${ELARA_PID_FILE:-${DEMO_ROOT}/.pids}"

if [[ -n "${PID_FILE}" ]]; then
  mkdir -p "$(dirname "${PID_FILE}")"
  printf "%s\t%s\n" "$$" "orange-fortress-epa-debug" >> "${PID_FILE}"
fi

cd "$ROOT_DIR"

TARGET="orange-fortress-epa-debug"
if [[ ! -x "./build/${TARGET}" ]]; then
  ./build.sh
fi
exec ./build/${TARGET} "$@"
