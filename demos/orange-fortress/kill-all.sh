#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_ROOT="$(cd "${ROOT_DIR}/../.." && pwd)"
CPP_DIR="${ROOT_DIR}/cpp"

declare -a PATTERNS=(
  "${CPP_DIR}/build/orange-fortress"
  "${CPP_DIR}/build/orange-fortress-epa-debug"
  "${FRAMEWORK_ROOT}/build/bin/elaraui-server"
  "${FRAMEWORK_ROOT}/libElaraUI/build/bin/elaraui-server"
)

found=0

for pattern in "${PATTERNS[@]}"; do
  while IFS= read -r pid; do
    [[ -z "${pid}" ]] && continue
    found=1
    echo "killing pid ${pid} for pattern: ${pattern}"
    kill "${pid}" 2>/dev/null || true
  done < <(ps -ef | grep -F "${pattern}" | grep -v grep | awk '{print $2}')
done

sleep 1

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
