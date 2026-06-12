#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ELARA_OS_HOME="${ELARA_OS_HOME:-$HOME/.elaraos}"
ROOT_DRIVE="${ELARA_OS_ROOT_DRIVE:-$ELARA_OS_HOME/root}"
ROOT_MOUNT="${ELARA_OS_ROOT_MOUNT:-$ELARA_OS_HOME/root.mount}"
PARTITION_OFFSET_BYTES="${ELARA_OS_PARTITION_OFFSET_BYTES:-1048576}"

if [[ ! -f "$ROOT_DRIVE" ]]; then
  echo "Missing root virtual drive: $ROOT_DRIVE"
  echo "Creating virtual drives first..."
  "${SCRIPT_DIR}/setup_virtual_drives.sh"
fi

mkdir -p "$ROOT_MOUNT"

if mountpoint -q "$ROOT_MOUNT"; then
  echo "Elara OS root virtual drive is already mounted at $ROOT_MOUNT"
  exit 0
fi

sudo mount -o loop,offset="$PARTITION_OFFSET_BYTES" "$ROOT_DRIVE" "$ROOT_MOUNT"
echo "Mounted $ROOT_DRIVE -> $ROOT_MOUNT"
