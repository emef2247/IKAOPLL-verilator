#!/usr/bin/env bash
set -euo pipefail
# Quick-run helper for VIKAOPLL_vltb
# Usage:
#  ./run_vltb_quick.sh [sim_cycles] [trace(0|1)] [events.csv]
# Examples:
#  ./run_vltb_quick.sh 1000000 1 events.csv
#  ./run_vltb_quick.sh 5200000 0

SIM_CYCLES=${1:-1000000}        # default: 1,000,000 half-ticks (short smoke test)
USE_TRACE=${2:-1}               # 1 = expect VCD (requires build with --trace), 0 = no VCD used
EVENTS_CSV=${3:-events.csv}
BINARY=./obj_dir/VIKAOPLL_vltb

# Ensure binary exists (build if missing)
if [ ! -x "${BINARY}" ]; then
  echo "[INFO] Binary ${BINARY} not found — run build_and_run_vltb.sh first (or use build_notrace)"
  exit 1
fi

echo "[INFO] Running ${BINARY} sim_cycles=${SIM_CYCLES} trace=${USE_TRACE} events=${EVENTS_CSV}"
LOG=run.log

# Run in background and tail log
# pass --verbose to get DBG_CPP prints; remove if noisy
"${BINARY}" "${SIM_CYCLES}" 3579545 44100 "${EVENTS_CSV}" 1024 --verbose > "${LOG}" 2>&1 &

PID=$!
echo "[INFO] started pid=${PID}, logging to ${LOG}"
sleep 0.2
tail -f "${LOG}"