#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

BUILD_PROFILE="${BUILD_PROFILE:-release}"
INSTALL_ROOT_DIR="${INSTALL_ROOT_DIR:-/usr/local}"
BUILD_ROOT="${ROOT_DIR}/build"

if [[ "${1:-}" == "--remove" ]]; then
  echo "Removing from ${INSTALL_ROOT_DIR}..."
  make remove BUILD_PROFILE="${BUILD_PROFILE}" INSTALL_ROOT_DIR="${INSTALL_ROOT_DIR}"
  exit 0
fi

if [[ ! -d "${BUILD_ROOT}/include" && ! -d "${BUILD_ROOT}/lib" && ! -d "${BUILD_ROOT}/bin" ]]; then
  echo "Missing local build artifacts under ${BUILD_ROOT}."
  echo "Run ./build.sh first, then rerun this installer."
  exit 1
fi

echo "Installing into ${INSTALL_ROOT_DIR}..."
make install BUILD_PROFILE="${BUILD_PROFILE}" INSTALL_ROOT_DIR="${INSTALL_ROOT_DIR}"
