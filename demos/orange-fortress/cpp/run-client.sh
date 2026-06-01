#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_ROOT="$(cd "${ROOT_DIR}/.." && pwd)"
PID_FILE="${ELARA_PID_FILE:-${DEMO_ROOT}/.pids}"

if [[ -n "${PID_FILE}" ]]; then
  mkdir -p "$(dirname "${PID_FILE}")"
  printf "%s\t%s\n" "$$" "orange-fortress-client" >> "${PID_FILE}"
fi

cd "$ROOT_DIR"

if [[ ! -x "./build/orange-fortress" ]]; then
  ./build.sh
fi

BRIDGE_PORT=""
BRIDGE_INFO_FILE="/tmp/elara-debug-bridge.json"
if [[ -f "${BRIDGE_INFO_FILE}" ]]; then
  BRIDGE_PORT=$(python3 -c "import json; d=json.load(open('${BRIDGE_INFO_FILE}')); print(d.get('host_debug_port',''))" 2>/dev/null || true)
fi

if [[ -n "${BRIDGE_PORT}" ]]; then
  exec ./build/orange-fortress "$@" "${BRIDGE_PORT}"
else
  exec ./build/orange-fortress "$@"
fi
