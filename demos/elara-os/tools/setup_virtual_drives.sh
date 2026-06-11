#!/usr/bin/env bash
set -euo pipefail

ELARA_OS_HOME="${ELARA_OS_HOME:-$HOME/.elaraos}"
ROOT_DRIVE="${ELARA_OS_ROOT_DRIVE:-$ELARA_OS_HOME/root}"
DATA_DRIVE="${ELARA_OS_DATA_DRIVE:-$ELARA_OS_HOME/data}"
DRIVE_SIZE_BYTES="${ELARA_OS_DRIVE_SIZE_BYTES:-1073741824}"

mkdir -p "$ELARA_OS_HOME"

prepare_drive() {
  local drive_path="$1"
  local part_name="$2"

  truncate -s "$DRIVE_SIZE_BYTES" "$drive_path"

  if ! parted -s "$drive_path" unit B print >/dev/null 2>&1; then
    parted -s "$drive_path" mklabel gpt
    parted -s "$drive_path" mkpart primary ext4 1MiB 100%
    parted -s "$drive_path" name 1 "$part_name"
  fi

  echo "Drive: $drive_path"
  parted -m "$drive_path" unit B print
  echo
}

prepare_drive "$ROOT_DRIVE" "rootfs"
prepare_drive "$DATA_DRIVE" "data"
