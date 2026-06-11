#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILDER="${ROOT_DIR}/libElaraParallelAssembly/e/epa_builder.py"

python3 "${BUILDER}" --project "${SCRIPT_DIR}" "$@"

# Keep the legacy path available while tools migrate to explicit targets.
if [ -f "${SCRIPT_DIR}/build/epa_firmware.bin" ]; then
  cp "${SCRIPT_DIR}/build/epa_firmware.bin" "${SCRIPT_DIR}/build/epa.bin"
fi

if [ "${ELARA_OS_SKIP_DISK_INSTALL:-0}" != "1" ] && [ -f "${SCRIPT_DIR}/build/epa_kernel.bin" ]; then
  "${SCRIPT_DIR}/tools/install_kernel_to_virtual_root.sh"
fi
