#!/usr/bin/env bash
set -euo pipefail
echo "[INFO] Building Verilator model (wrapper top: IKAOPLL_vltb)"

# adjust paths to your project layout if needed
verilator -Wall --cc --trace --Mdir obj_dir --top-module IKAOPLL_vltb \
  --Wno-PINCONNECTEMPTY --Wno-DECLFILENAME --Wno-WIDTH --Wno-UNUSED \
  --Wno-UNDRIVEN --Wno-CASEINCOMPLETE \
  src/IKAOPLL.v src/IKAOPLL_modules/*.v src/IKAOPLL_vltb.sv --exe tb/tb_ikaopll_vgm_verilator.cpp

make -C obj_dir -f VIKAOPLL_vltb.mk -j"$(nproc)" VIKAOPLL_vltb

echo "[INFO] Run (example): ./obj_dir/VIKAOPLL_vltb <sim_cycles> <emuclk_hz> <audio_sr> <events.csv> [reset_half_cycles]"
echo "[INFO] Example: ./obj_dir/VIKAOPLL_vltb 47000000 3579545 44100 ym2413_scale_chromatic.delta.csv 8192"

