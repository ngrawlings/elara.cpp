#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_ROOT="$(cd "${ROOT_DIR}/../.." && pwd)"
CPP_DIR="${ROOT_DIR}/cpp"
EPA_DIR="${ROOT_DIR}/epa"
BUILD_DIR="${ROOT_DIR}/build"
ARTIFACT_DIR="${ROOT_DIR}/artifacts"
PID_FILE="${ROOT_DIR}/.pids"

UI_PORT="${UI_PORT:-18798}"
EPA_DEBUG_PORT="${EPA_DEBUG_PORT:-18878}"
UI_HOST="${UI_HOST:-127.0.0.1}"
EPA_DEBUG_HOST="${EPA_DEBUG_HOST:-127.0.0.1}"

UI_LOG="${ARTIFACT_DIR}/ui-head-${UI_PORT}.log"
EPA_DEBUG_LOG="${ARTIFACT_DIR}/epa-debug-${EPA_DEBUG_PORT}.log"
APP_LOG="${ARTIFACT_DIR}/orange-fortress-${UI_PORT}.log"

UI_PID=""
EPA_DEBUG_PID=""
EPA_DEBUG_OK=0

cleanup() {
  if [[ -n "${EPA_DEBUG_PID}" ]] && kill -0 "${EPA_DEBUG_PID}" 2>/dev/null; then
    kill "${EPA_DEBUG_PID}" 2>/dev/null || true
    wait "${EPA_DEBUG_PID}" 2>/dev/null || true
  fi
  if [[ -n "${UI_PID}" ]] && kill -0 "${UI_PID}" 2>/dev/null; then
    kill "${UI_PID}" 2>/dev/null || true
    wait "${UI_PID}" 2>/dev/null || true
  fi
}

trap cleanup EXIT

mkdir -p "${ARTIFACT_DIR}" "${BUILD_DIR}"

if [[ -x "${ROOT_DIR}/kill-all.sh" ]]; then
  echo "[orange-fortress] clearing existing related processes"
  "${ROOT_DIR}/kill-all.sh" >/dev/null 2>&1 || true
fi

: > "${PID_FILE}"
export ELARA_PID_FILE="${PID_FILE}"

echo "[orange-fortress] rebuilding EPA runtime"
make -C "${FRAMEWORK_ROOT}/libElaraParallelAssembly" -j2 >/dev/null
make -C "${FRAMEWORK_ROOT}/libElaraParallelAssembly/e" -j2 >/dev/null
if ! make -C "${FRAMEWORK_ROOT}/libElaraUI" rpc-server -j2 >/dev/null; then
  if [[ -x "${FRAMEWORK_ROOT}/libElaraUI/build/bin/elaraui-server" ]]; then
    echo "[orange-fortress] warning: staged UI server copy failed, using fresh local UI server binary"
  else
    echo "[orange-fortress] UI server build failed and no fresh local binary is available"
    exit 1
  fi
fi

echo "[orange-fortress] building EPA bundle -> ${BUILD_DIR}/epa.bin"
"${FRAMEWORK_ROOT}/libElaraParallelAssembly/build/e/e2epabin" \
  --out "${BUILD_DIR}/epa.bin" \
  "${EPA_DIR}/entry.e" \
  "${EPA_DIR}/gameplay_rules.e" \
  "${EPA_DIR}/input_dispatch.e" \
  "${EPA_DIR}/player_avatar.e" \
  "${EPA_DIR}/player_machinegun.e" \
  "${EPA_DIR}/scene.e" \
  "${EPA_DIR}/scene_compiler.e" \
  "${EPA_DIR}/test.e" \
  "${EPA_DIR}/walls.e" \
  "${EPA_DIR}/render_scene.e" \
  "${EPA_DIR}/render_ui.e" \
  "${EPA_DIR}/world_runtime.e"

echo "[orange-fortress] building C++ host"
( cd "${CPP_DIR}" && ./build.sh >/dev/null )

echo "[orange-fortress] starting UI head on ${UI_HOST}:${UI_PORT}"
( cd "${CPP_DIR}" && ./run-ui-head.sh --port "${UI_PORT}" >"${UI_LOG}" 2>&1 ) &
UI_PID="$!"
sleep 1
if ! kill -0 "${UI_PID}" 2>/dev/null; then
  echo "UI head failed to start. See ${UI_LOG}"
  exit 1
fi

echo "[orange-fortress] starting EPA debug RPC on ${EPA_DEBUG_HOST}:${EPA_DEBUG_PORT}"
( cd "${CPP_DIR}" && ./run-epa-debug.sh "${EPA_DEBUG_PORT}" "${EPA_DEBUG_HOST}" >"${EPA_DEBUG_LOG}" 2>&1 ) &
EPA_DEBUG_PID="$!"
sleep 1
if ! kill -0 "${EPA_DEBUG_PID}" 2>/dev/null; then
  EPA_DEBUG_PID=""
  echo "[orange-fortress] warning: EPA debug RPC failed to start. See ${EPA_DEBUG_LOG}"
else
  EPA_DEBUG_OK=1
fi

echo "[orange-fortress] logs:"
echo "  UI head:   ${UI_LOG}"
echo "  EPA debug: ${EPA_DEBUG_LOG}"
echo "  App:       ${APP_LOG}"
echo "  Trace:     ${ARTIFACT_DIR}/live-epa-trace.jsonl"
echo "  PID file:  ${PID_FILE}"
echo "  UI RPC:    ${UI_HOST}:${UI_PORT}"
if [[ "${EPA_DEBUG_OK}" -eq 1 ]]; then
  echo "  EPA RPC:   ${EPA_DEBUG_HOST}:${EPA_DEBUG_PORT}"
else
  echo "  EPA RPC:   unavailable"
fi

echo "[orange-fortress] launching client"
cd "${CPP_DIR}"
./run-client.sh "${UI_HOST}" "${UI_PORT}" 2>&1 | tee "${APP_LOG}"
