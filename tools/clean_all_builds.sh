#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

echo "Cleaning build artifacts under: $ROOT_DIR"

mapfile -t BUILD_DIRS < <(
  find . \
    \( -path './.git' -o -path './artifacts' -o -path './valgrind-logs' \) -prune -o \
    -type d \( -name build -o -name 'build.stale.*' \) -print \
  | sort
)

if [ "${#BUILD_DIRS[@]}" -eq 0 ]; then
  echo "No build directories found."
  exit 0
fi

printf 'Removing:\n'
printf '  %s\n' "${BUILD_DIRS[@]}"

quarantined=()

for dir in "${BUILD_DIRS[@]}"; do
  if rm -rf -- "$dir" 2>/dev/null; then
    continue
  fi

  quarantine="${dir}.quarantine.$(date +%s)"
  echo "Quarantining unwritable directory: $dir -> $quarantine"
  mv -- "$dir" "$quarantine"
  quarantined+=("$quarantine")
done

echo "Build cleanup complete."

if [ "${#quarantined[@]}" -gt 0 ]; then
  printf 'Quarantined (not deleted due to ownership/permissions):\n'
  printf '  %s\n' "${quarantined[@]}"
fi
