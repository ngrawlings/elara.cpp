#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_ROOT="$(cd "${ROOT_DIR}/../.." && pwd)"
CPP_DIR="${ROOT_DIR}/cpp"
PID_FILE="${ROOT_DIR}/.pids"

pid_matches_orange_fortress() {
  local pid="$1"
  local args
  args="$(ps -p "${pid}" -o args= 2>/dev/null || true)"
  [[ -n "${args}" ]] || return 1
  [[ "${args}" == *"orange-fortress"* ]] && return 0
  [[ "${args}" == *"elaraui-server"* ]] && return 0
  return 1
}

kill_pid_file_entries() {
  local signal="$1"
  local force_label="$2"
  [[ -f "${PID_FILE}" ]] || return 0
  while IFS=$'\t' read -r pid label; do
    [[ -z "${pid}" ]] && continue
    if ! pid_matches_orange_fortress "${pid}"; then
      continue
    fi
    found=1
    if [[ "${signal}" == "-9" ]]; then
      echo "force killing pid ${pid} from .pids${label:+ (${label})}"
      kill -9 "${pid}" 2>/dev/null || true
    else
      echo "killing pid ${pid} from .pids${label:+ (${label})}"
      kill "${pid}" 2>/dev/null || true
    fi
  done < "${PID_FILE}"
}

declare -a PATTERNS=(
  "${CPP_DIR}/build/orange-fortress"
  "${CPP_DIR}/build/orange-fortress-epa-debug"
  "${FRAMEWORK_ROOT}/build/bin/elaraui-server"
  "${FRAMEWORK_ROOT}/libElaraUI/build/bin/elaraui-server"
)

found=0

kill_pid_file_entries "" ""

for pattern in "${PATTERNS[@]}"; do
  while IFS= read -r pid; do
    [[ -z "${pid}" ]] && continue
    found=1
    echo "killing pid ${pid} for pattern: ${pattern}"
    kill "${pid}" 2>/dev/null || true
  done < <(ps -ef | grep -F "${pattern}" | grep -v grep | awk '{print $2}')
done

sleep 1

kill_pid_file_entries "-9" "force"

for pattern in "${PATTERNS[@]}"; do
  while IFS= read -r pid; do
    [[ -z "${pid}" ]] && continue
    echo "force killing pid ${pid} for pattern: ${pattern}"
    kill -9 "${pid}" 2>/dev/null || true
  done < <(ps -ef | grep -F "${pattern}" | grep -v grep | awk '{print $2}')
done

if [[ "${found}" -eq 0 ]]; then
  echo "no orange fortress related processes found"
fi

rm -f "${PID_FILE}"
