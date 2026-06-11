#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

export ELARA_OS_SKIP_DISK_INSTALL=1
"${PROJECT_DIR}/build_epa.sh" --target kernel
unset ELARA_OS_SKIP_DISK_INSTALL

ELARA_OS_KERNEL_IMAGE="${ELARA_OS_KERNEL_IMAGE:-${PROJECT_DIR}/build/epa_kernel.bin}" \
  "${SCRIPT_DIR}/install_kernel_to_virtual_root.sh"
