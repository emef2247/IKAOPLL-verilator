#!/usr/bin/env bash
# scripts/run_verilator.sh
# Wrapper to build+run the verilator VLTB model. Uses build_and_run_vltb.sh if present,
# otherwise runs a fallback verilator build command.
#
# Usage:
#   ./scripts/run_verilator.sh [sim_cycles] [emuclk_hz] [audio_sr] [events.csv] [reset_half_cycles] [TRACE(0|1)]
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_SCRIPT="${ROOT}/build_and_run_vltb.sh"
BUILD_DIR="${ROOT}/obj_dir"
EXE_NAME="VIKAOPLL_vltb"
SIM_CYCLES="${1:-400000}"
EMUCLK_HZ="${2:-3579545}"
AUDIO_SR="${3:-44100}"
EVENTS_CSV="${4:-tests/data/ym2413_scale_chromatic.vgm.csv}"
RESET_HALF="${5:-1024}"
TRACE_FLAG="${6:-0}"   # 1 to enable VCD tracing (if build script / verilator called with --trace)

cd "${ROOT}"

# Ensure tests/data exists
if [ ! -f "${EVENTS_CSV}" ]; then
  echo "[WARN] events CSV not found: ${EVENTS_CSV}"
fi

if [ -x "${BUILD_SCRIPT}" ]; then
  echo "[INFO] Using existing build script: ${BUILD_SCRIPT}"
  if [ "${TRACE_FLAG}" -eq 1 ]; then
    # If build script supports --trace by default it will produce dump.vcd
    "${BUILD_SCRIPT}"
  else
    "${BUILD_SCRIPT}"
  fi
else
  echo "[INFO] build_and_run_vltb.sh not found or not executable; running fallback Verilator build"
  verilator -Wall --cc --trace --Mdir obj_dir --top-module IKAOPLL_vltb \
    --Wno-PINCONNECTEMPTY --Wno-DECLFILENAME --Wno-WIDTH --Wno-UNUSED \
    src/IKAOPLL.v src/IKAOPLL_modules/*.v src/IKAOPLL_vltb.sv --exe tb/tb_ikaopll_vgm_verilator.cpp
  make -C obj_dir -f VIKAOPLL_vltb.mk -j"$(nproc)" "${EXE_NAME}"
fi

# Determine exe path (support both naming conventions)
EXE_PATH="${ROOT}/obj_dir/${EXE_NAME}"
if [ ! -x "${EXE_PATH}" ]; then
  # fallback: try VIKAOPLL (some builds use different top)
  if [ -x "${ROOT}/obj_dir/VIKAOPLL" ]; then
    EXE_PATH="${ROOT}/obj_dir/VIKAOPLL"
  else
    echo "[ERR] executable not found in obj_dir: looked for ${EXE_NAME} and VIKAOPLL" >&2
    ls -la obj_dir || true
    exit 2
  fi
fi

echo "[INFO] Running: ${EXE_PATH} ${SIM_CYCLES} ${EMUCLK_HZ} ${AUDIO_SR} ${EVENTS_CSV} ${RESET_HALF}"
# Run, tee stdout/stderr to logfile for inspection
LOG="${ROOT}/build_verilator_run.log"
mkdir -p "$(dirname "${LOG}")"
"${EXE_PATH}" "${SIM_CYCLES}" "${EMUCLK_HZ}" "${AUDIO_SR}" "${EVENTS_CSV}" "${RESET_HALF}" 2>&1 | tee "${LOG}"
echo "[INFO] Run finished; log: ${LOG}"

