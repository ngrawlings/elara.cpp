#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_ROOT="$(mktemp -d /tmp/elara-generated-projects.XXXXXX)"
BUILDER_BIN="${TMP_ROOT}/elara-project-builder"
LINT_BIN="${ROOT_DIR}/build/bin/elara.cpp-lint"
INSTALL_ROOT="${TMP_ROOT}/install-root"

cleanup() {
  rm -rf "${TMP_ROOT}"
}

trap cleanup EXIT

require_file() {
  local path="$1"

  if [[ ! -e "${path}" ]]; then
    echo "Missing required path: ${path}" >&2
    exit 1
  fi
}

build_builder() {
  require_file "${ROOT_DIR}/build/include"
  require_file "${ROOT_DIR}/build/lib/libelaracore.a"
  require_file "${ROOT_DIR}/build/lib/libelaraio.a"
  require_file "${LINT_BIN}"

  g++ -std=gnu++11 -O3 -DNDEBUG \
    -I"${ROOT_DIR}/ElaraProjectBuilder" \
    -I"${ROOT_DIR}/build/include" \
    "${ROOT_DIR}/ElaraProjectBuilder/src/main.cpp" \
    "${ROOT_DIR}/ElaraProjectBuilder/src/projectbuilder/ProjectBuilder.cpp" \
    -L"${ROOT_DIR}/build/lib" \
    -lelaraio -lelaracore \
    -o "${BUILDER_BIN}"
}

assert_cli_contract() {
  local help_output=""
  local error_output=""
  local invalid_output=""
  local temp_dir="${TMP_ROOT}/cli-contract"

  help_output="$("${BUILDER_BIN}" --help)"
  if [[ "${help_output}" != *"Usage:"* || "${help_output}" != *"--socket-mode <none|server|client>"* ]]; then
    echo "Builder help output is missing expected content" >&2
    exit 1
  fi

  if "${BUILDER_BIN}" --port 4040 >"${TMP_ROOT}/cli-port.stdout" 2>"${TMP_ROOT}/cli-port.stderr"; then
    echo "Builder accepted --port without socket mode" >&2
    exit 1
  fi
  error_output="$(cat "${TMP_ROOT}/cli-port.stderr")"
  if [[ "${error_output}" != *"--address and --port require --socket-mode server or --socket-mode client"* ]]; then
    echo "Builder returned the wrong error for port without socket mode" >&2
    exit 1
  fi

  if "${BUILDER_BIN}" --socket-mode server --port >"${TMP_ROOT}/cli-missing-port.stdout" 2>"${TMP_ROOT}/cli-missing-port.stderr"; then
    echo "Builder accepted missing --port value" >&2
    exit 1
  fi
  error_output="$(cat "${TMP_ROOT}/cli-missing-port.stderr")"
  if [[ "${error_output}" != *"Missing value for --port"* ]]; then
    echo "Builder returned the wrong error for missing --port value" >&2
    exit 1
  fi

  if "${BUILDER_BIN}" --bogus >"${TMP_ROOT}/cli-bogus.stdout" 2>"${TMP_ROOT}/cli-bogus.stderr"; then
    echo "Builder accepted an unknown flag" >&2
    exit 1
  fi
  invalid_output="$(cat "${TMP_ROOT}/cli-bogus.stderr")"
  if [[ "${invalid_output}" != *"Unknown option: --bogus"* ]]; then
    echo "Builder returned the wrong error for an unknown flag" >&2
    exit 1
  fi

  "${BUILDER_BIN}" --non-interactive --output "${temp_dir}" --name CliContract --target cli-contract-app --repl yes
  if [[ ! -f "${temp_dir}/src/main.cpp" ]]; then
    echo "Builder failed to generate a project in non-interactive mode" >&2
    exit 1
  fi
}

assert_real_agent_doc() {
  local project_dir="$1"

  if grep -q "Agent reference asset not found" "${project_dir}/ELARA_AGENT_API.md"; then
    echo "Generated project at ${project_dir} contains placeholder agent documentation" >&2
    exit 1
  fi
}

exercise_project() {
  local label="$1"
  local target_name="$2"
  shift 2

  local project_dir="${TMP_ROOT}/${label}"
  local project_install_root="${INSTALL_ROOT}/${label}"

  echo "[${label}] generating"
  "${BUILDER_BIN}" --non-interactive --output "${project_dir}" "$@"

  assert_real_agent_doc "${project_dir}"

  echo "[${label}] building"
  (
    cd "${project_dir}"
    ELARA_ROOT="${ROOT_DIR}/build" ./build.sh
  )

  echo "[${label}] linting"
  (
    cd "${project_dir}"
    make lint ELARA_CPP_LINT="${LINT_BIN}"
  )

  echo "[${label}] installing"
  (
    cd "${project_dir}"
    INSTALL_PREFIX="${project_install_root}" ./install.sh
  )

  if [[ ! -x "${project_install_root}/bin/${target_name}" ]]; then
    echo "Expected installed binary missing: ${project_install_root}/bin/${target_name}" >&2
    exit 1
  fi

  if [[ ! -f "${project_install_root}/share/${target_name}/install-manifest.txt" ]]; then
    echo "Expected install manifest missing for ${label}" >&2
    exit 1
  fi

  echo "[${label}] removing"
  (
    cd "${project_dir}"
    INSTALL_PREFIX="${project_install_root}" ./install.sh --remove
  )

  if [[ -e "${project_install_root}/bin/${target_name}" ]]; then
    echo "Installed binary still present after remove: ${project_install_root}/bin/${target_name}" >&2
    exit 1
  fi
}

build_builder
assert_cli_contract

exercise_project \
  plain \
  smoke-plain \
  --name SmokePlain \
  --target smoke-plain \
  --repl yes

exercise_project \
  worker \
  smoke-worker \
  --name SmokeWorker \
  --target smoke-worker \
  --worker yes \
  --worker-name SmokeWorkerTask

exercise_project \
  socket-server \
  smoke-socket-server \
  --name SmokeSocketServer \
  --target smoke-socket-server \
  --socket-mode server \
  --address 0.0.0.0 \
  --port 4040

exercise_project \
  socket-client \
  smoke-socket-client \
  --name SmokeSocketClient \
  --target smoke-socket-client \
  --socket-mode client \
  --address 127.0.0.1 \
  --port 4040

echo "Smoke tests passed."
