#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

if [[ ! -x "./build/epa-debug-smoke" ]]; then
  ./build.sh
fi
exec ./build/epa-debug-smoke "$@"
