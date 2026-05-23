#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

BUILD_PROFILE="${BUILD_PROFILE:-release}"
FRAMEWORK_ROOT="${FRAMEWORK_ROOT:-$(cd "${ROOT_DIR}/.." && pwd)}"
ELARA_ROOT="${ELARA_ROOT:-${FRAMEWORK_ROOT}/build}"

autoreconf -fi
./configure FRAMEWORK_ROOT="${FRAMEWORK_ROOT}" ELARA_ROOT="${ELARA_ROOT}"
make BUILD_PROFILE="${BUILD_PROFILE}" FRAMEWORK_ROOT="${FRAMEWORK_ROOT}" ELARA_ROOT="${ELARA_ROOT}" "$@"
