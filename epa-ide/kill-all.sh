#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="${ROOT_DIR}/.pids"

pid_matches_epa_ide() {
  local pid="$1"
  local args
  args="$(ps -p "${pid}" -o args= 2>/dev/null || true)"
  [[ -n "${args}" ]] || return 1
  [[ "${args}" == *"/app.py"* ]] && return 0
  [[ "${args}" == *"elaraui-server"* ]] && return 0
  [[ "${args}" == *"/ai_rpc_client.py"* ]] && return 0
  [[ "${args}" == *" epa-ide/ai_rpc_client.py "* ]] && return 0
  [[ "${args}" == *"epavm"* ]] && return 0
  [[ "${args}" == *"epa-dbg"* ]] && return 0
  [[ "${args}" == *"gdb --interpreter=mi2"* ]] && return 0
  [[ "${args}" == *"epa-signal-lab"* ]] && return 0
  [[ "${args}" == *"orange-fortress"* ]] && return 0
  return 1
}

kill_pid_file_entries() {
  local signal="$1"
  [[ -f "${PID_FILE}" ]] || return 0
  while IFS=$'\t' read -r pid label; do
    [[ -n "${pid}" ]] || continue
    if ! pid_matches_epa_ide "${pid}"; then
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

kill_matching_processes() {
  local signal="$1"
  local grep_pattern="$2"
  local pid
  while IFS= read -r pid; do
    [[ -n "${pid}" ]] || continue
    found=1
    if [[ "${signal}" == "-9" ]]; then
      echo "force killing pid ${pid} for pattern: ${grep_pattern}"
      kill -9 "${pid}" 2>/dev/null || true
    else
      echo "killing pid ${pid} for pattern: ${grep_pattern}"
      kill "${pid}" 2>/dev/null || true
    fi
  done < <(ps -ef | grep -E "${grep_pattern}" | grep -v grep | awk '{print $2}')
}

found=0

kill_pid_file_entries ""

kill_matching_processes "" 'python3 ./app.py'
kill_matching_processes "" 'python3 (.+/epa-ide/ai_rpc_client.py|epa-ide/ai_rpc_client.py) --port'
kill_matching_processes "" 'elaraui-server'
kill_matching_processes "" '/epavm '
kill_matching_processes "" '/epa-dbg '
kill_matching_processes "" 'gdb --interpreter=mi2'
kill_matching_processes "" '/epa-signal-lab/cpp/build/epa-signal-lab '
kill_matching_processes "" '/orange-fortress/cpp/build/orange-fortress'

sleep 1

kill_pid_file_entries "-9"

kill_matching_processes "-9" 'python3 ./app.py'
kill_matching_processes "-9" 'python3 (.+/epa-ide/ai_rpc_client.py|epa-ide/ai_rpc_client.py) --port'
kill_matching_processes "-9" 'elaraui-server'
kill_matching_processes "-9" '/epavm '
kill_matching_processes "-9" '/epa-dbg '
kill_matching_processes "-9" 'gdb --interpreter=mi2'
kill_matching_processes "-9" '/epa-signal-lab/cpp/build/epa-signal-lab '
kill_matching_processes "-9" '/orange-fortress/cpp/build/orange-fortress'

if [[ "${found}" -eq 0 ]]; then
  echo "no EPA IDE related processes found"
fi

rm -f "${PID_FILE}"
