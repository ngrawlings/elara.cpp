#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

MODE_ARG="--debug"
if [[ "${1:-}" == "--debug" ]]; then
    shift
fi
PORT="${1:-18878}"
ADDRESS="${2:-127.0.0.1}"

if [[ ! -x "./build/epavm" ]]; then
    echo "epavm not built. Run ./build.sh first."
    exit 1
fi

exec ./build/epavm "$MODE_ARG" "$PORT" "$ADDRESS"
