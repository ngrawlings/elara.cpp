#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_ROOT="$(cd "${ROOT_DIR}/../../.." 2>/dev/null && pwd)"
LOCAL_FRESH_SERVER="${FRAMEWORK_ROOT}/libElaraUI/build/bin/elaraui-server"
LOCAL_STAGED_SERVER="${FRAMEWORK_ROOT}/build/bin/elaraui-server"
DEFAULT_SERVER="/usr/local/bin/elaraui-server"
if [[ -x "${LOCAL_FRESH_SERVER}" ]]; then
  RESOLVED_SERVER="${LOCAL_FRESH_SERVER}"
elif [[ -x "${LOCAL_STAGED_SERVER}" ]]; then
  RESOLVED_SERVER="${LOCAL_STAGED_SERVER}"
else
  RESOLVED_SERVER="${DEFAULT_SERVER}"
fi
ELARA_UI_SERVER="${ELARA_UI_SERVER:-${RESOLVED_SERVER}}"

if [[ ! -x "${ELARA_UI_SERVER}" ]]; then
  echo "Missing Elara UI server at ${ELARA_UI_SERVER}. Install the framework server or set ELARA_UI_SERVER to the correct path."
  exit 1
fi

exec "${ELARA_UI_SERVER}" "$@"
