#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
RTL_DIR="${ROOT_DIR}/rtl"
SRC_DIR="${ROOT_DIR}/src"
mkdir -p "${BUILD_DIR}"
if ! command -v verilator >/dev/null; then echo 'Verilator is required.' >&2; exit 1; fi
if [[ ! -f "${RTL_DIR}/IKAOPLL.v" ]]; then echo 'Prepare rtl/ first (see README).' >&2; exit 1; fi
RTL_SOURCES=("${RTL_DIR}/IKAOPLL.v")
while IFS= read -r -d '' f; do RTL_SOURCES+=("$f"); done < <(find "${RTL_DIR}/IKAOPLL_modules" -type f -name '*.v' -print0 | sort -z)
cd "${BUILD_DIR}"
# Match the supplied working hardware/sample configuration explicitly.
# In the supplied RTL, the nonpipelined PG branch has an inverted reset condition.
verilator --cc --exe --trace --top-module IKAOPLL \
  -GFULLY_SYNCHRONOUS=1 -GFAST_RESET=1 -GUSE_PIPELINED_MULTIPLIER=1 \
  --Wno-PINCONNECTEMPTY --Wno-DECLFILENAME --Wno-UNUSED --Wno-WIDTH \
  "${RTL_SOURCES[@]}" \
  "${SRC_DIR}/ikaopll_wrapper.cpp" "${SRC_DIR}/ym2413_bus.c" \
  "${SRC_DIR}/vgm_player.c" "${SRC_DIR}/vgm_parser.c" \
  "${SRC_DIR}/wav_writer.c" "${SRC_DIR}/main_vgm.c" \
  -CFLAGS '-O3 -std=c++11' -LDFLAGS '-lz' -o ikaopll_sim
make -C obj_dir -f VIKAOPLL.mk -j2 ikaopll_sim
if [[ "${1:-}" == '--build-only' ]]; then exit 0; fi
cd "${ROOT_DIR}"
if [[ $# -eq 0 ]]; then set -- tests/vgm/ym2413_scale_rom1.vgm; fi
# No traces or diagnostic CSVs unless requested; WAV is always captured.
set +e
"${BUILD_DIR}/obj_dir/ikaopll_sim" "$@" > "${BUILD_DIR}/sim_last_output.txt" 2>&1
result=$?
set -e
cat "${BUILD_DIR}/sim_last_output.txt"
exit "${result}"
