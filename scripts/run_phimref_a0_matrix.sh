#!/usr/bin/env bash
# Run 4 combinations of PHIMREF_INVERT (arg7) and A0_ACTIVE_HIGH (arg9)
# Usage: ./scripts/run_phimref_a0_matrix.sh
set -euo pipefail
EXE=./obj_dir/VIKAOPLL_vltb
CSV=small_events.csv
RESET=256
WR_HOLD=1
EMUCLK=3579545
SR=44100

if [ ! -x "$EXE" ]; then
  echo "ERROR: executable $EXE not found. Build first with ./build_and_run_vltb.sh"
  exit 2
fi
if [ ! -f "$CSV" ]; then
  echo "ERROR: $CSV not found. Create it with head -n 40 <bigfile> > $CSV"
  exit 2
fi

mkdir -p test_runs
for PHIM in 0 1; do
  for A0 in 0 1; do
    TAG="pi${PHIM}_a0${A0}"
    LOG="test_runs/run_test_${TAG}.log"
    VCD="test_runs/dump_${TAG}.vcd"
    echo "=== Running PHIMREF_INVERT=${PHIM} A0_ACTIVE_HIGH=${A0} -> ${TAG} ==="
    # remove old dump.vcd to avoid mv race
    rm -f dump.vcd
    # run with line-buffered output
    stdbuf -oL "$EXE" 50000 "$EMUCLK" "$SR" "$CSV" "$RESET" "$WR_HOLD" "$PHIM" "99" "$A0" 2>&1 | tee "$LOG"
    # If VCD was produced, move it
    if [ -f dump.vcd ]; then
      mv -f dump.vcd "$VCD"
      echo "VCD saved to $VCD"
    else
      echo "No dump.vcd generated for ${TAG} (maybe VM_TRACE disabled)"
    fi
    echo "=== Finished ${TAG} (log: $LOG) ==="
    sleep 0.5
  done
done

echo "All runs done. Logs and VCDs are in test_runs/"

