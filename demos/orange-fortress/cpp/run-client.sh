#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

if [[ ! -x "./build/orange-fortress" ]]; then
  ./build.sh
fi
exec ./build/orange-fortress "$@"
