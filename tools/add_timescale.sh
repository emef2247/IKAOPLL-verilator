#!/usr/bin/env bash
# Add `timescale 1ps/1ps` to Verilog files under src/ if not present.
# Usage: ./tools/add_timescale.sh
set -euo pipefail

files=$(git ls-files 'src/*.v' 'src/IKAOPLL_modules/*.v' 2>/dev/null || true)

if [ -z "$files" ]; then
  echo "No target .v files found under src/ or src/IKAOPLL_modules/."
  exit 0
fi

echo "Files to check:"
printf '%s\n' $files

for f in $files; do
  if grep -q -E '^[[:space:]]*`timescale' "$f"; then
    echo "SKIP: $f (already has `timescale)"
  else
    echo "PREPEND: $f"
    # Use sed to insert a line at file top (portable on Linux)
    sed -i '1i`timescale 1ps/1ps' "$f"
  fi
done

echo "Done. Please git diff and review changes, then re-run ./build_and_run_vltb.sh"

