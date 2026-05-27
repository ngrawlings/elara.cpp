#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

BUILD_PROFILE="${BUILD_PROFILE:-debug}"
ARTIFACT_ROOT="${ARTIFACT_ROOT:-./artifacts}"
TEST_DURATION_SECONDS="${TEST_DURATION_SECONDS:-${1:-30}}"

autoreconf -fi
./configure
make stress BUILD_PROFILE="${BUILD_PROFILE}" ARTIFACT_ROOT="${ARTIFACT_ROOT}" TEST_DURATION_SECONDS="${TEST_DURATION_SECONDS}"
