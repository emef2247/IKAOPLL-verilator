#!/usr/bin/env bash
# Simple release run helper:
# - Builds (invokes existing build script)
# - Runs a full simulation to create WAV
# - Compresses dump.vcd to dump.vcd.gz
# - Packs release artifacts into release_package.tar.gz
#
# Usage:
#   ./scripts/release_run.sh --sim-cycles 47000000 --emuclk 3579545 --sr 44100 --events events.csv --reset 8192

set -euo pipefail
SIM_CYCLES=${SIM_CYCLES:-47000000}
EMUCLK_HZ=${EMUCLK_HZ:-3579545}
SR=${SR:-44100}
EVENTS=${EVENTS:-ym2413_scale_chromatic.delta.csv}
RESET_HALF=${RESET_HALF:-8192}
WR_HOLD=${WR_HOLD:-3}
TRACE_LEVEL=${TRACE_LEVEL:-99}

# parse args
while [[ $# -gt 0 ]]; do
  case $1 in
    --sim-cycles) SIM_CYCLES="$2"; shift 2;;
    --emuclk) EMUCLK_HZ="$2"; shift 2;;
    --sr) SR="$2"; shift 2;;
    --events) EVENTS="$2"; shift 2;;
    --reset) RESET_HALF="$2"; shift 2;;
    --wr-hold) WR_HOLD="$2"; shift 2;;
    --trace-level) TRACE_LEVEL="$2"; shift 2;;
    *) echo "Unknown arg $1"; exit 1;;
  esac
done

echo "[INFO] Building"
./build_and_run_vltb.sh

LOG=run_vltb.log
rm -f "$LOG" dump.vcd dump.vcd.gz out_from_vgm.wav

echo "[INFO] Running simulation (this may take a while)..."
# note: harness reads WR_HOLD from CLI if implemented; otherwise edit TB to consume WR_HOLD env
stdbuf -oL -eL ./obj_dir/VIKAOPLL_vltb "$SIM_CYCLES" "$EMUCLK_HZ" "$SR" "$EVENTS" "$RESET_HALF" 2>&1 | tee "$LOG"

if [ -f dump.vcd ]; then
  echo "[INFO] compressing dump.vcd -> dump.vcd.gz"
  gzip -9 -c dump.vcd > dump.vcd.gz
else
  echo "[WARN] dump.vcd not found"
fi

# Pack artifacts
PKG=release_package.tar.gz
tar czf "$PKG" out_from_vgm.wav "$EVENTS" "$LOG" dump.vcd.gz || true
echo "[INFO] Created package: $PKG"
ls -lh out_from_vgm.wav "$PKG" dump.vcd.gz "$LOG"

