#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="${ELARA_PID_FILE:-${ROOT_DIR}/.pids}"

if [[ -n "${PID_FILE}" ]]; then
  mkdir -p "$(dirname "${PID_FILE}")"
  printf "%s\t%s\n" "$$" "epa-ide-ai-rpc-client" >> "${PID_FILE}"
fi

cd "${ROOT_DIR}"

exec python3 ./ai_rpc_client.py "$@"
