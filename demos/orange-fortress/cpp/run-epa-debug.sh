#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

TARGET="orange-fortress-epa-debug"
if [[ ! -x "./build/${TARGET}" ]]; then
  ./build.sh
fi
exec ./build/${TARGET} "$@"
