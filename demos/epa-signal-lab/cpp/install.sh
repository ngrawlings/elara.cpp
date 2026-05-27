#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

BUILD_PROFILE="${BUILD_PROFILE:-release}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
TARGET_NAME="epa-signal-lab"
BUILD_TARGET="$ROOT_DIR/build/${TARGET_NAME}"
MANIFEST_DIR="${INSTALL_PREFIX}/share/${TARGET_NAME}"
MANIFEST_PATH="${MANIFEST_DIR}/install-manifest.txt"

if [[ "${1:-}" == "--remove" ]]; then
  echo "Removing from ${INSTALL_PREFIX}..."
  if [[ -f "${MANIFEST_PATH}" ]]; then
    while IFS= read -r installed_path; do
      rm -f "${installed_path}"
    done < "${MANIFEST_PATH}"
    rm -f "${MANIFEST_PATH}"
    rmdir "${MANIFEST_DIR}" 2>/dev/null || true
  else
    make remove BUILD_PROFILE="${BUILD_PROFILE}" PREFIX="${INSTALL_PREFIX}"
  fi
  exit 0
fi

if [[ ! -x "$BUILD_TARGET" ]]; then
  echo "Missing built binary $BUILD_TARGET. Run ./build.sh first, then rerun this installer."
  exit 1
fi

echo "Installing into ${INSTALL_PREFIX}..."
make install BUILD_PROFILE="${BUILD_PROFILE}" PREFIX="${INSTALL_PREFIX}"
mkdir -p "${MANIFEST_DIR}"
printf '%s\n' "${INSTALL_PREFIX}/bin/${TARGET_NAME}" > "${MANIFEST_PATH}"
