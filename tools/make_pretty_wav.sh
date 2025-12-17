#!/usr/bin/env bash
# tools/make_pretty_wav.sh
# Wrapper: build, run, and create a "pretty" WAV using default processing
# Usage: ./tools/make_pretty_wav.sh [out.wav]
set -euo pipefail

OUT=${1:-out_pretty.wav}
CSV=audio_samples.csv

echo "[pretty] Running build_and_run.sh (this generates ${CSV})..."
./build_and_run.sh

if [ ! -f "$CSV" ]; then
  echo "ERROR: ${CSV} not found after simulation."
  exit 1
fi

echo "[pretty] Postprocessing CSV -> WAV (defaults: tau=1.5ms acc=0.3 lpf=1500 softclip off normalize on)..."
python3 tools/make_wav_from_audio_csv_expdecay.py "$CSV" "$OUT" --tau_ms 1.5 --mo-gain 1.0 --acc-gain 0.3 --lpf-fc 1500

echo "[pretty] WAV written to $OUT"

