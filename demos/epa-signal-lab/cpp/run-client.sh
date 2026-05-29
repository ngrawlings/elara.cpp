#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

if [[ ! -x "./build/epa-signal-lab" ]]; then
  ./build.sh
fi

BRIDGE_PORT=""
BRIDGE_INFO_FILE="/tmp/elara-debug-bridge.json"
if [[ -f "${BRIDGE_INFO_FILE}" ]]; then
  BRIDGE_PORT=$(python3 -c "import json; d=json.load(open('${BRIDGE_INFO_FILE}')); print(d.get('host_debug_port',''))" 2>/dev/null || true)
fi

if [[ -n "${BRIDGE_PORT}" ]]; then
  exec ./build/epa-signal-lab "$@" "${BRIDGE_PORT}"
else
  exec ./build/epa-signal-lab "$@"
fi
