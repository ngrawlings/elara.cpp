#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

mkdir -p ./build
python3 -m py_compile app.py elara_ui/__init__.py elara_ui/builder.py elara_ui/rpc.py
printf '%s\n' "python build ok" > ./build/build-stamp.txt
