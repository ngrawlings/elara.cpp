#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEMO_ROOT="$(cd "${ROOT_DIR}/.." && pwd)"
PID_FILE="${ELARA_PID_FILE:-${DEMO_ROOT}/.pids}"

if [[ -n "${PID_FILE}" ]]; then
  mkdir -p "$(dirname "${PID_FILE}")"
  printf "%s\t%s\n" "$$" "elara-os-client" >> "${PID_FILE}"
fi

cd "$ROOT_DIR"

if [[ ! -x "./build/elara-os" ]]; then
  ./build.sh
fi

USE_IDE_HOST_BRIDGE="${ELARA_OS_USE_IDE_HOST_BRIDGE:-0}"
BRIDGE_INFO_FILE="${ELARA_DEBUG_BRIDGE_FILE:-/tmp/elara-debug-bridge.json}"
if [[ "$USE_IDE_HOST_BRIDGE" == "1" && -f "$BRIDGE_INFO_FILE" ]]; then
  export ELARA_IDE_HOST_BRIDGE_HOST="${ELARA_IDE_HOST_BRIDGE_HOST:-$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("host_debug_host","127.0.0.1"))' "$BRIDGE_INFO_FILE" 2>/dev/null || printf '127.0.0.1')}"
  export ELARA_IDE_HOST_BRIDGE_PORT="${ELARA_IDE_HOST_BRIDGE_PORT:-$(python3 -c 'import json,sys; value=json.load(open(sys.argv[1])).get("host_debug_port",0); print(value if value else "")' "$BRIDGE_INFO_FILE" 2>/dev/null || true)}"
else
  unset ELARA_IDE_HOST_BRIDGE_HOST
  unset ELARA_IDE_HOST_BRIDGE_PORT
fi

exec ./build/elara-os "$@"
