#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="${ELARA_PID_FILE:-${ROOT_DIR}/.pids}"

if [[ -n "${PID_FILE}" ]]; then
  mkdir -p "$(dirname "${PID_FILE}")"
  printf "%s\t%s\n" "$$" "epa-ide-client" >> "${PID_FILE}"
fi

cd "$ROOT_DIR"

AI_RPC_PORT="${AI_RPC_PORT:-18792}"
args=("$@")
has_ai_rpc_port=0

for arg in "${args[@]}"; do
  if [[ "${arg}" == "--ai-rpc-port" ]] || [[ "${arg}" == --ai-rpc-port=* ]]; then
    has_ai_rpc_port=1
    break
  fi
done

if [[ "${has_ai_rpc_port}" -eq 0 ]]; then
  args=(--ai-rpc-port "${AI_RPC_PORT}" "${args[@]}")
fi

exec python3 ./app.py "${args[@]}"
