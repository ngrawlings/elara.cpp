#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

BUILD_PROFILE="${BUILD_PROFILE:-asan}"
ARTIFACT_ROOT="${ARTIFACT_ROOT:-./artifacts}"
TEST_DURATION_SECONDS="${TEST_DURATION_SECONDS:-${1:-30}}"

autoreconf -fi
./configure
ASAN_OPTIONS="detect_leaks=0" LSAN_OPTIONS="detect_leaks=0" make fuzz BUILD_PROFILE="${BUILD_PROFILE}" ARTIFACT_ROOT="${ARTIFACT_ROOT}" TEST_DURATION_SECONDS="${TEST_DURATION_SECONDS}"
