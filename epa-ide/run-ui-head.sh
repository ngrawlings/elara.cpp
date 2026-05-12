#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${ROOT_DIR}/.." && pwd)"
LOCAL_FRAMEWORK_SERVER="${WORKSPACE_ROOT}/build/bin/elaraui-server"
DEFAULT_SERVER="/usr/local/bin/elaraui-server"
if [[ -x "${LOCAL_FRAMEWORK_SERVER}" ]]; then
  RESOLVED_SERVER="${LOCAL_FRAMEWORK_SERVER}"
else
  RESOLVED_SERVER="${DEFAULT_SERVER}"
fi
ELARA_UI_SERVER="${ELARA_UI_SERVER:-${RESOLVED_SERVER}}"

if [[ ! -x "${ELARA_UI_SERVER}" ]]; then
  echo "Missing Elara UI server at ${ELARA_UI_SERVER}. Install the framework server or set ELARA_UI_SERVER to the correct path."
  exit 1
fi

exec "${ELARA_UI_SERVER}" "$@"
