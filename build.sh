#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

PROJECT_FLAGS=(
  "libelaracore"
  "libelaradebug"
  "libelarathreads"
  "libelaraencryption"
  "libelaraevent"
  "libelaraformat"
  "libelaraio"
  "libelaradata"
  "libelarasockets"
  "libelaraserializer"
  "elaraunittests"
  "elaraprojectbuilder"
  "elaracpplint"
)

FEATURE_FLAGS=(
  "mysql"
  "sqlite"
  "xml"
)

BUILD_PROFILES=(
  "release"
  "debug"
  "asan"
)

prompt_yes_no() {
  local prompt="$1"
  local reply

  while true; do
    read -r -p "$prompt [Y/n] " reply
    reply="${reply:-y}"
    case "$reply" in
      [Yy]|[Yy][Ee][Ss]) return 0 ;;
      [Nn]|[Nn][Oo]) return 1 ;;
      *) echo "Please answer y or n." ;;
    esac
  done
}

run_interactive() {
  local -a configure_args=()
  local flag
  local profile="release"

  echo "Select build options."

  for flag in "${PROJECT_FLAGS[@]}"; do
    if ! prompt_yes_no "Build ${flag}?"; then
      configure_args+=("--disable-${flag}")
    fi
  done

  for flag in "${FEATURE_FLAGS[@]}"; do
    if ! prompt_yes_no "Enable ${flag}?"; then
      configure_args+=("--disable-${flag}")
    fi
  done

  echo
  echo "Choose build profile."
  echo "  1. release - fast optimized build"
  echo "  2. debug   - debug symbols, easier debugging"
  echo "  3. asan    - AddressSanitizer + UndefinedBehaviorSanitizer"
  select profile in "${BUILD_PROFILES[@]}"; do
    if [[ -n "${profile:-}" ]]; then
      break
    fi
    echo "Please choose a valid profile number."
  done

  echo
  echo "Running autoreconf..."
  autoreconf -fi

  echo "Running configure..."
  if ((${#configure_args[@]} > 0)); then
    ./configure "${configure_args[@]}"
  else
    ./configure
  fi

  echo "Running make..."
  make BUILD_PROFILE="$profile"
}

run_matrix() {
  local -a all_flags=("${PROJECT_FLAGS[@]}" "${FEATURE_FLAGS[@]}")
  local total_flags=${#all_flags[@]}
  local total_combinations=$((1 << total_flags))
  local mask
  local i
  local -a args

  autoreconf -fi

  for ((mask = 0; mask < total_combinations; mask++)); do
    args=()
    for ((i = 0; i < total_flags; i++)); do
      if (( (mask & (1 << i)) == 0 )); then
        args+=("--disable-${all_flags[i]}")
      fi
    done

    printf '\n[%d/%d] ./configure' "$((mask + 1))" "$total_combinations"
    if ((${#args[@]} > 0)); then
      printf ' %s' "${args[@]}"
    fi
    printf '\n'

    if ! ./configure "${args[@]}"; then
      echo "Skipping make: configure rejected this combination"
      continue
    fi

    make remove >/dev/null 2>&1 || true
    make clean >/dev/null 2>&1 || true
    make
  done

  ./configure
}

run_smoke() {
  echo "Running framework smoke validation..."
  autoreconf -fi
  ./configure
  make smoke BUILD_PROFILE="${BUILD_PROFILE:-release}"
}

case "${1:-interactive}" in
  interactive)
    run_interactive
    ;;
  --matrix)
    run_matrix
    ;;
  --smoke)
    run_smoke
    ;;
  *)
    echo "Usage: $0 [interactive|--matrix|--smoke]" >&2
    exit 1
    ;;
esac
