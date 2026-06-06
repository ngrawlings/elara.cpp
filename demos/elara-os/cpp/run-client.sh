#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

if [[ ! -x "./build/elara-os" ]]; then
  ./build.sh
fi

BRIDGE_INFO_FILE="${ELARA_DEBUG_BRIDGE_FILE:-/tmp/elara-debug-bridge.json}"
if [[ -f "$BRIDGE_INFO_FILE" ]]; then
  export ELARA_IDE_HOST_BRIDGE_HOST="${ELARA_IDE_HOST_BRIDGE_HOST:-$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("host_debug_host","127.0.0.1"))' "$BRIDGE_INFO_FILE" 2>/dev/null || printf '127.0.0.1')}"
  export ELARA_IDE_HOST_BRIDGE_PORT="${ELARA_IDE_HOST_BRIDGE_PORT:-$(python3 -c 'import json,sys; value=json.load(open(sys.argv[1])).get("host_debug_port",0); print(value if value else "")' "$BRIDGE_INFO_FILE" 2>/dev/null || true)}"
fi

exec ./build/elara-os "$@"
