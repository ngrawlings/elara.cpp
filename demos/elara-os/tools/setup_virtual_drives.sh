#!/usr/bin/env bash
set -euo pipefail

ELARA_OS_HOME="${ELARA_OS_HOME:-$HOME/.elaraos}"
ROOT_DRIVE="${ELARA_OS_ROOT_DRIVE:-$ELARA_OS_HOME/root}"
DATA_DRIVE="${ELARA_OS_DATA_DRIVE:-$ELARA_OS_HOME/data}"
DRIVE_SIZE_BYTES="${ELARA_OS_DRIVE_SIZE_BYTES:-1073741824}"
PARTITION_OFFSET_BYTES="${ELARA_OS_PARTITION_OFFSET_BYTES:-1048576}"
FS_BLOCK_SIZE="${ELARA_OS_FS_BLOCK_SIZE:-4096}"

mkdir -p "$ELARA_OS_HOME"

prepare_drive() {
  local drive_path="$1"
  local part_name="$2"
  local part_size_bytes
  local fs_blocks

  truncate -s "$DRIVE_SIZE_BYTES" "$drive_path"

  if ! parted -s "$drive_path" unit B print >/dev/null 2>&1; then
    parted -s "$drive_path" mklabel gpt
    parted -s "$drive_path" mkpart primary ext4 1MiB 100%
    parted -s "$drive_path" name 1 "$part_name"
  fi

  part_size_bytes="$(parted -m "$drive_path" unit B print | awk -F: '$1 == "1" { gsub(/B/, "", $4); print $4; }')"
  if [[ -z "$part_size_bytes" ]]; then
    echo "Could not determine partition size for $drive_path" >&2
    return 1
  fi
  fs_blocks=$((part_size_bytes / FS_BLOCK_SIZE))

  if [[ "${ELARA_OS_FORMAT_EXT4:-1}" == "1" ]]; then
    mkfs.ext4 -F -q -b "$FS_BLOCK_SIZE" -L "$part_name" -E offset="$PARTITION_OFFSET_BYTES" "$drive_path" "$fs_blocks"
  fi

  echo "Drive: $drive_path"
  parted -m "$drive_path" unit B print
  echo
}

prepare_drive "$ROOT_DRIVE" "rootfs"
prepare_drive "$DATA_DRIVE" "data"
