#!/usr/bin/env bash
set -euo pipefail

ELARA_OS_HOME="${ELARA_OS_HOME:-$HOME/.elaraos}"
ROOT_MOUNT="${ELARA_OS_ROOT_MOUNT:-$ELARA_OS_HOME/root.mount}"

if ! mountpoint -q "$ROOT_MOUNT"; then
  echo "Elara OS root virtual drive is not mounted at $ROOT_MOUNT"
  exit 0
fi

sudo umount "$ROOT_MOUNT"
echo "Unmounted $ROOT_MOUNT"
