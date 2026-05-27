#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
TARGET_NAME="epa-signal-lab"
APP_DIR="${INSTALL_PREFIX}/share/${TARGET_NAME}"
BIN_PATH="${INSTALL_PREFIX}/bin/${TARGET_NAME}"
MANIFEST_PATH="${APP_DIR}/install-manifest.txt"

if [[ "${1:-}" == "--remove" ]]; then
  if [[ -f "${MANIFEST_PATH}" ]]; then
    while IFS= read -r installed_path; do rm -f "${installed_path}"; done < "${MANIFEST_PATH}"
    rm -f "${MANIFEST_PATH}"
  fi
  rm -rf "${APP_DIR}"
  rm -f "${BIN_PATH}"
  exit 0
fi

mkdir -p "${APP_DIR}" "${INSTALL_PREFIX}/bin" "${APP_DIR}/elara_ui"
cp app.py "${APP_DIR}/app.py"
cp elara_ui/__init__.py "${APP_DIR}/elara_ui/__init__.py"
cp elara_ui/builder.py "${APP_DIR}/elara_ui/builder.py"
cp elara_ui/rpc.py "${APP_DIR}/elara_ui/rpc.py"
cat > "${BIN_PATH}" <<EOF
#!/usr/bin/env bash
exec python3 "${APP_DIR}/app.py" "\$@"
EOF
chmod +x "${BIN_PATH}"
printf '%s\n' "${BIN_PATH}" > "${MANIFEST_PATH}"
printf '%s\n' "${APP_DIR}/app.py" >> "${MANIFEST_PATH}"
printf '%s\n' "${APP_DIR}/elara_ui/__init__.py" >> "${MANIFEST_PATH}"
printf '%s\n' "${APP_DIR}/elara_ui/builder.py" >> "${MANIFEST_PATH}"
printf '%s\n' "${APP_DIR}/elara_ui/rpc.py" >> "${MANIFEST_PATH}"
