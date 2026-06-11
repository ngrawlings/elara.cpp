#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

ELARA_OS_HOME="${ELARA_OS_HOME:-$HOME/.elaraos}"
ROOT_DRIVE="${ELARA_OS_ROOT_DRIVE:-$ELARA_OS_HOME/root}"
KERNEL_IMAGE="${ELARA_OS_KERNEL_IMAGE:-$PROJECT_DIR/build/epa_kernel.bin}"
PARTITION_OFFSET_BYTES="${ELARA_OS_PARTITION_OFFSET_BYTES:-1048576}"
FS_BLOCK_SIZE="${ELARA_OS_FS_BLOCK_SIZE:-4096}"

if [[ ! -f "$KERNEL_IMAGE" ]]; then
  echo "Missing kernel image: $KERNEL_IMAGE" >&2
  exit 1
fi

if [[ ! -f "$ROOT_DRIVE" ]]; then
  "${SCRIPT_DIR}/setup_virtual_drives.sh"
fi

part_size_bytes="$(parted -m "$ROOT_DRIVE" unit B print | awk -F: '$1 == "1" { gsub(/B/, "", $4); print $4; }')"
if [[ -z "$part_size_bytes" ]]; then
  echo "Could not determine root partition size for $ROOT_DRIVE" >&2
  exit 1
fi

fs_blocks=$((part_size_bytes / FS_BLOCK_SIZE))
stage_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$stage_dir"
}
trap cleanup EXIT

mkdir -p "$stage_dir/boot/elara" "$stage_dir/proc" "$stage_dir/etc"
cp "$KERNEL_IMAGE" "$stage_dir/boot/elara/epa_kernel.bin"
cat > "$stage_dir/boot/elara/kernel.manifest" <<EOF
name=elara-os
image=epa_kernel.bin
format=epa-bundle
stage=second-stage
EOF

mkfs.ext4 -F -q -b "$FS_BLOCK_SIZE" -L rootfs -d "$stage_dir" \
  -E offset="$PARTITION_OFFSET_BYTES" "$ROOT_DRIVE" "$fs_blocks"

echo "Installed $KERNEL_IMAGE -> $ROOT_DRIVE:/boot/elara/epa_kernel.bin"
