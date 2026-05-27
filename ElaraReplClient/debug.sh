#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

BUILD_PROFILE="${BUILD_PROFILE:-debug}"
ARTIFACT_ROOT="${ARTIFACT_ROOT:-./artifacts}"

autoreconf -fi
./configure
make debug BUILD_PROFILE="${BUILD_PROFILE}" ARTIFACT_ROOT="${ARTIFACT_ROOT}" "$@"
