#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

BUILD_PROFILE="${BUILD_PROFILE:-release}"
INSTALL_ROOT_DIR="${INSTALL_ROOT_DIR:-/usr/local}"
BUILD_ROOT="${ROOT_DIR}/build"
MANIFEST_DIR="${INSTALL_ROOT_DIR}/share/elara.cpp"
MANIFEST_PATH="${MANIFEST_DIR}/install-manifest.txt"

write_manifest() {
  mkdir -p "${MANIFEST_DIR}"
  : > "${MANIFEST_PATH}"

  local source_path=""
  local relative_path=""

  while IFS= read -r -d '' source_path; do
    relative_path="${source_path#${BUILD_ROOT}/}"
    printf '%s\n' "${INSTALL_ROOT_DIR}/${relative_path}" >> "${MANIFEST_PATH}"
  done < <(find "${BUILD_ROOT}/bin" "${BUILD_ROOT}/include" "${BUILD_ROOT}/lib" "${BUILD_ROOT}/share" -type f -print0 2>/dev/null)
}

remove_from_manifest() {
  local installed_path=""

  if [[ ! -f "${MANIFEST_PATH}" ]]; then
    return 1
  fi

  while IFS= read -r installed_path; do
    [[ -n "${installed_path}" ]] || continue
    rm -f "${installed_path}"
  done < "${MANIFEST_PATH}"

  rm -f "${MANIFEST_PATH}"
  rmdir "${INSTALL_ROOT_DIR}/share/elara-project-builder" 2>/dev/null || true
  rmdir "${MANIFEST_DIR}" 2>/dev/null || true
  rmdir "${INSTALL_ROOT_DIR}/share" 2>/dev/null || true
}

if [[ "${1:-}" == "--remove" ]]; then
  echo "Removing from ${INSTALL_ROOT_DIR}..."
  if ! remove_from_manifest; then
    make remove BUILD_PROFILE="${BUILD_PROFILE}" INSTALL_ROOT_DIR="${INSTALL_ROOT_DIR}"
  fi
  exit 0
fi

if [[ ! -d "${BUILD_ROOT}/include" && ! -d "${BUILD_ROOT}/lib" && ! -d "${BUILD_ROOT}/bin" ]]; then
  echo "Missing local build artifacts under ${BUILD_ROOT}."
  echo "Run ./build.sh first, then rerun this installer."
  exit 1
fi

echo "Installing into ${INSTALL_ROOT_DIR}..."
make install BUILD_PROFILE="${BUILD_PROFILE}" INSTALL_ROOT_DIR="${INSTALL_ROOT_DIR}"
write_manifest
