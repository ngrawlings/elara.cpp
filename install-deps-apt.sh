#!/usr/bin/env bash

set -euo pipefail

APT_GET="${APT_GET:-apt-get}"
SUDO="${SUDO:-}"

if [[ "$(id -u)" -ne 0 && -z "${SUDO}" ]]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  else
    echo "This script needs root privileges. Re-run as root or set SUDO to your privilege command."
    exit 1
  fi
fi

if ! command -v "${APT_GET}" >/dev/null 2>&1; then
  echo "Could not find ${APT_GET}. This installer is for apt-based systems."
  exit 1
fi

packages=(
  autoconf
  automake
  build-essential
  libtool
  openssl
  pkg-config
  libssl-dev
  libsecp256k1-dev
  libevent-dev
  libgtk-4-dev
)

echo "Updating apt package lists..."
"${SUDO}" "${APT_GET}" update

echo "Installing build dependencies: ${packages[*]}"
"${SUDO}" "${APT_GET}" install -y "${packages[@]}"

if [[ ! -f /usr/include/openssl/rsa.h ]]; then
  echo "OpenSSL headers are still missing: /usr/include/openssl/rsa.h"
  echo "Expected apt package: libssl-dev"
  exit 1
fi

if ! pkg-config --exists gtk4; then
  echo "GTK4 pkg-config metadata is still missing."
  echo "Expected apt package: libgtk-4-dev"
  exit 1
fi

echo "Dependency installation complete."
echo "OpenSSL headers found. Re-run ./build.sh or ./configure && make so configure can detect them."
