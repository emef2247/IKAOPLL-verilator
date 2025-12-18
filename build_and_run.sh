#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OBJ_DIR="${BUILD_DIR}/obj_dir"

RTL_DIR="${ROOT_DIR}/rtl"
SRC_DIR="${ROOT_DIR}/src"

# First positional arg is the CSV path (same default as other scripts).
SIM_CSV=${1:-vgm_data/tests/ym2413_scale_chromatic.vgm.csv}
# Shift so "$@" will contain any additional runtime flags (e.g. --vcd, --no-csv, vcd filename)
if [ $# -gt 0 ]; then
  shift 1
fi

# Keep clean-rebuild behaviour (as requested)
rm -rf "${OBJ_DIR}"
mkdir -p "${OBJ_DIR}"

# Collect RTL sources (same as build_and_run.sh)
RTL_SOURCES=("${RTL_DIR}/IKAOPLL.v")
while IFS= read -r -d '' f; do
  RTL_SOURCES+=("$f")
done < <(find "${RTL_DIR}/IKAOPLL_modules" -type f -name '*.v' -print0)

echo "RTL sources:"
printf '  %s\n' "${RTL_SOURCES[@]}"

# Run Verilator with --trace enabled (only change from build_and_run.sh is enabling --trace)
echo "[build-debug] Running Verilator with --trace ..."
cd "${ROOT_DIR}"
verilator --cc "${RTL_SOURCES[@]}" --exe --trace \
  -Mdir "${OBJ_DIR}" -O2 \
  --Wno-PINCONNECTEMPTY --Wno-DECLFILENAME --Wno-UNUSED --Wno-WIDTH \
  "${SRC_DIR}/ikaopll_wrapper.cpp" "${SRC_DIR}/ym2413_bus.c" "${SRC_DIR}/vgm_player.c" "${SRC_DIR}/wav_writer.c" "${SRC_DIR}/main_vgm_csv.c" \
  -o ikaopll_sim

# Build via generated Makefile (same as build_and_run.sh)
echo "[build-debug] make in ${OBJ_DIR} ..."
make -C "${OBJ_DIR}" -f VIKAOPLL.mk ikaopll_sim

# Run simulator.
# NOTE: main_vgm_csv.c already supports runtime flags (--vcd, --no-csv, etc).
# We forward any extra args the user provided to this script to the simulator so
# VCD ON/OFF is controlled at runtime (e.g. pass --vcd to enable VCD).
echo "[run-debug] Running simulator (trace build). Pass --vcd to enable VCD output."
"${OBJ_DIR}/ikaopll_sim" "${SIM_CSV}" "$@"

echo "Done."