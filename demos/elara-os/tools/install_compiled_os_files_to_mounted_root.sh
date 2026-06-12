#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${PROJECT_DIR}/../.." && pwd)"

ELARA_OS_HOME="${ELARA_OS_HOME:-$HOME/.elaraos}"
ROOT_MOUNT="${ELARA_OS_ROOT_MOUNT:-$ELARA_OS_HOME/root.mount}"
KERNEL_IMAGE="${ELARA_OS_KERNEL_IMAGE:-$PROJECT_DIR/build/epa_kernel.bin}"
SHELL_IMAGE="${ELARA_OS_SHELL_IMAGE:-$PROJECT_DIR/build/app/elara_shell.epa.bin}"
PNG_DECODER_LIB="${ELARA_OS_PNG_DECODER_LIB:-$PROJECT_DIR/build/lib/png_decoder.epa.bin}"
LOGO_IMAGE="${ELARA_OS_LOGO_IMAGE:-$REPO_DIR/logo.png}"

if ! mountpoint -q "$ROOT_MOUNT"; then
  echo "Elara OS root is not mounted at $ROOT_MOUNT" >&2
  echo "Run demos/elara-os/tools/mount_virtual_root.sh first." >&2
  exit 1
fi

if [[ ! -f "$KERNEL_IMAGE" ]]; then
  echo "Missing kernel image: $KERNEL_IMAGE" >&2
  echo "Build it with: ELARA_OS_SKIP_DISK_INSTALL=1 demos/elara-os/build_epa.sh --target kernel" >&2
  exit 1
fi

if [[ ! -f "$SHELL_IMAGE" ]]; then
  echo "Missing shell process image: $SHELL_IMAGE" >&2
  echo "Build it with: ELARA_OS_SKIP_DISK_INSTALL=1 demos/elara-os/build_epa.sh --target shell_process" >&2
  exit 1
fi

if [[ ! -f "$PNG_DECODER_LIB" ]]; then
  echo "Missing PNG decoder library: $PNG_DECODER_LIB" >&2
  echo "Build it with: ELARA_OS_SKIP_DISK_INSTALL=1 demos/elara-os/build_epa.sh --target png_decoder_lib" >&2
  exit 1
fi

if [[ ! -f "$LOGO_IMAGE" ]]; then
  echo "Missing logo image: $LOGO_IMAGE" >&2
  exit 1
fi

INSTALL_AS=()
if [[ ! -w "$ROOT_MOUNT" && ! -w "$ROOT_MOUNT/boot" ]]; then
  INSTALL_AS=(sudo)
fi

"${INSTALL_AS[@]}" install -d "$ROOT_MOUNT/boot/elara/apps" "$ROOT_MOUNT/boot/elara/assets" "$ROOT_MOUNT/boot/elara/lib" "$ROOT_MOUNT/proc" "$ROOT_MOUNT/etc"
"${INSTALL_AS[@]}" install -m 0644 "$KERNEL_IMAGE" "$ROOT_MOUNT/boot/elara/epa_kernel.bin"
"${INSTALL_AS[@]}" install -m 0644 "$SHELL_IMAGE" "$ROOT_MOUNT/boot/elara/apps/shell.epa.bin"
"${INSTALL_AS[@]}" install -m 0644 "$PNG_DECODER_LIB" "$ROOT_MOUNT/boot/elara/lib/png_decoder.epa.bin"
"${INSTALL_AS[@]}" install -m 0644 "$LOGO_IMAGE" "$ROOT_MOUNT/boot/elara/assets/logo.png"

tmp_manifest="$(mktemp)"
cleanup() {
  rm -f "$tmp_manifest"
}
trap cleanup EXIT

cat > "$tmp_manifest" <<EOF
name=elara-os
image=epa_kernel.bin
format=epa-bundle
stage=second-stage
shell=/boot/elara/apps/shell.epa.bin
png_decoder=/boot/elara/lib/png_decoder.epa.bin
logo=/boot/elara/assets/logo.png
EOF

"${INSTALL_AS[@]}" install -m 0644 "$tmp_manifest" "$ROOT_MOUNT/boot/elara/kernel.manifest"

echo "Installed compiled Elara OS files into $ROOT_MOUNT"
echo "  /boot/elara/epa_kernel.bin <- $KERNEL_IMAGE"
echo "  /boot/elara/apps/shell.epa.bin <- $SHELL_IMAGE"
echo "  /boot/elara/lib/png_decoder.epa.bin <- $PNG_DECODER_LIB"
echo "  /boot/elara/kernel.manifest"
echo "  /boot/elara/assets/logo.png <- $LOGO_IMAGE"
