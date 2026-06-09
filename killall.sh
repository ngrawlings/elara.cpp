#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRY_RUN=0

if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=1
  shift
fi

if [[ $# -gt 0 ]]; then
  echo "usage: $0 [--dry-run]" >&2
  exit 2
fi

declare -A PPID_OF=()
declare -A CMD_OF=()
declare -A COMM_OF=()
declare -A TARGETS=()
declare -A REASONS=()

DIRECT_PATTERNS=(
  "elaraui-server"
  "gdb --interpreter=mi2"
  "gdb /usr/local/bin/elaraui-server"
  "gdb /home/nyhl/workspace/elara.cpp"
  "epa-ide/ai_rpc_client.py"
  "/epavm "
  "/epa-dbg "
  "orange-fortress"
  "epa-signal-lab"
  "elara-os"
  "python3 ./app.py"
  "python ./app.py"
)

pid_in_project_tree() {
  local pid="$1"
  local cwd
  cwd="$(readlink "/proc/${pid}/cwd" 2>/dev/null || true)"
  [[ -n "${cwd}" ]] || return 1
  [[ "${cwd}" == "${PROJECT_ROOT}" || "${cwd}" == "${PROJECT_ROOT}/"* ]]
}

cmd_matches_project() {
  local cmd="$1"
  [[ "${cmd}" == *"${PROJECT_ROOT}"* ]] && return 0
  local pattern
  for pattern in "${DIRECT_PATTERNS[@]}"; do
    [[ "${cmd}" == *"${pattern}"* ]] && return 0
  done
  return 1
}

comm_matches_project() {
  local comm="$1"
  [[ "${comm}" == "elaraui-server" ]] && return 0
  [[ "${comm}" == "gdb" ]] && return 0
  [[ "${comm}" == "epavm" ]] && return 0
  [[ "${comm}" == "epa-dbg" ]] && return 0
  [[ "${comm}" == "elara-os" ]] && return 0
  return 1
}

mark_target() {
  local pid="$1"
  local reason="$2"
  [[ -n "${pid}" ]] || return 0
  [[ "${pid}" =~ ^[0-9]+$ ]] || return 0
  [[ "${pid}" != "$$" ]] || return 0
  [[ "${pid}" != "${PPID:-0}" ]] || return 0
  kill -0 "${pid}" 2>/dev/null || return 0
  if [[ -z "${TARGETS[$pid]:-}" ]]; then
    TARGETS["$pid"]=1
    REASONS["$pid"]="$reason"
  fi
}

for proc_dir in /proc/[0-9]*; do
  pid="${proc_dir#/proc/}"
  [[ -r "${proc_dir}/status" ]] || continue
  [[ -r "${proc_dir}/cmdline" ]] || continue
  ppid="$(awk '/^PPid:/ { print $2 }' "${proc_dir}/status" 2>/dev/null || true)"
  comm="$(cat "${proc_dir}/comm" 2>/dev/null || true)"
  cmd="$(tr '\0' ' ' < "${proc_dir}/cmdline" 2>/dev/null || true)"
  [[ -n "${pid:-}" ]] || continue
  [[ -n "${ppid:-}" ]] || continue
  [[ -n "${comm:-}" ]] || continue
  [[ -n "${cmd:-}" ]] || continue
  PPID_OF["$pid"]="$ppid"
  COMM_OF["$pid"]="$comm"
  CMD_OF["$pid"]="$cmd"
  if pid_in_project_tree "$pid"; then
    mark_target "$pid" "cwd under ${PROJECT_ROOT}"
    continue
  fi
  if comm_matches_project "$comm" && cmd_matches_project "$cmd"; then
    mark_target "$pid" "process name and command matched project pattern"
    continue
  fi
  if cmd_matches_project "$cmd"; then
    mark_target "$pid" "command matched project pattern"
  fi
done

while IFS= read -r line; do
  [[ -n "${line:-}" ]] || continue
  pid="$(awk '{print $2}' <<<"${line}")"
  ppid="$(awk '{print $3}' <<<"${line}")"
  cmd="$(awk '{for (i = 8; i <= NF; ++i) printf "%s%s", $i, (i < NF ? OFS : "")}' <<<"${line}")"
  [[ -n "${pid:-}" ]] || continue
  [[ -n "${ppid:-}" ]] || continue
  [[ -n "${cmd:-}" ]] || continue
  PPID_OF["$pid"]="${PPID_OF[$pid]:-$ppid}"
  CMD_OF["$pid"]="${CMD_OF[$pid]:-$cmd}"
  if [[ "${cmd}" == *"elaraui-server"* || "${cmd}" == *"gdb /usr/local/bin/elaraui-server"* ]]; then
    mark_target "$pid" "ps fallback matched elaraui-server"
  fi
done < <(ps -ef)

while IFS= read -r pid_file; do
  [[ -f "${pid_file}" ]] || continue
  while IFS=$'\t' read -r pid _label; do
    mark_target "$pid" "listed in ${pid_file#${PROJECT_ROOT}/}"
  done < "${pid_file}"
done < <(find "${PROJECT_ROOT}" -maxdepth 3 -type f -name '.pids' 2>/dev/null)

changed=1
while [[ "${changed}" -eq 1 ]]; do
  changed=0
  for pid in "${!PPID_OF[@]}"; do
    parent="${PPID_OF[$pid]}"
    if [[ -n "${TARGETS[$parent]:-}" && -z "${TARGETS[$pid]:-}" ]]; then
      TARGETS["$pid"]=1
      REASONS["$pid"]="child of ${parent}"
      changed=1
    fi
  done
done

if [[ "${#TARGETS[@]}" -eq 0 ]]; then
  find "${PROJECT_ROOT}" -maxdepth 3 -type f -name '.pids' -exec rm -f {} +
  echo "no elara.cpp related processes found"
  exit 0
fi

mapfile -t ORDERED_PIDS < <(printf '%s\n' "${!TARGETS[@]}" | sort -n)

echo "project root: ${PROJECT_ROOT}"
echo "matched ${#ORDERED_PIDS[@]} process(es)"
for pid in "${ORDERED_PIDS[@]}"; do
  printf '  pid=%s reason=%s cmd=%s\n' \
    "${pid}" \
    "${REASONS[$pid]}" \
    "${CMD_OF[$pid]:-(exited)}"
done

if [[ "${DRY_RUN}" -eq 1 ]]; then
  exit 0
fi

for pid in "${ORDERED_PIDS[@]}"; do
  kill "${pid}" 2>/dev/null || true
done

sleep 1

survivors=()
for pid in "${ORDERED_PIDS[@]}"; do
  if kill -0 "${pid}" 2>/dev/null; then
    survivors+=("${pid}")
  fi
done

if [[ "${#survivors[@]}" -gt 0 ]]; then
  echo "force killing ${#survivors[@]} surviving process(es)"
  for pid in "${survivors[@]}"; do
    kill -9 "${pid}" 2>/dev/null || true
  done
fi

find "${PROJECT_ROOT}" -maxdepth 3 -type f -name '.pids' -exec rm -f {} +

echo "cleanup complete"
