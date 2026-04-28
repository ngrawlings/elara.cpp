#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

BUILD_PROFILE="${BUILD_PROFILE:-release}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
TARGET_NAME="elara-repl-client"
BUILD_TARGET="$ROOT_DIR/build/${TARGET_NAME}"

if [[ "${1:-}" == "--remove" ]]; then
  echo "Removing from ${INSTALL_PREFIX}..."
  make remove BUILD_PROFILE="${BUILD_PROFILE}" PREFIX="${INSTALL_PREFIX}"
  exit 0
fi

if [[ ! -x "$BUILD_TARGET" ]]; then
  echo "Missing built binary $BUILD_TARGET. Run ./build.sh first, then rerun this installer."
  exit 1
fi

echo "Installing into ${INSTALL_PREFIX}..."
make install BUILD_PROFILE="${BUILD_PROFILE}" PREFIX="${INSTALL_PREFIX}"
