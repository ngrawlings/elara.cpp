#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PID_FILE="${ELARA_PID_FILE:-${PROJECT_DIR}/.pids}"
mkdir -p "$(dirname "$PID_FILE")"
printf "%s\t%s\n" "$$" "elara-os-launcher" >> "$PID_FILE"

cd "$PROJECT_DIR"

export ELARA_OS_USE_IDE_HOST_BRIDGE="${ELARA_OS_USE_IDE_HOST_BRIDGE:-0}"

if [[ "${ELARA_OS_BUILD_FIRST:-1}" != "0" ]]; then
  ELARA_OS_SKIP_DISK_INSTALL=1 ./build_epa.sh
  ./cpp/build.sh
fi

exec ./cpp/run-client.sh "$@"
