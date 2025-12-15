#!/usr/bin/env bash
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

RTL_DIR="${ROOT_DIR}/rtl"
SRC_DIR="${ROOT_DIR}/src"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# RTL ソースを列挙
RTL_SOURCES=(
  "${RTL_DIR}/IKAOPLL.v"
)

# IKAOPLL_modules 配下の .v を全部追加
while IFS= read -r -d '' f; do
  RTL_SOURCES+=("$f")
done < <(find "${RTL_DIR}/IKAOPLL_modules" -type f -name '*.v' -print0)

echo "RTL sources:"
printf '  %s\n' "${RTL_SOURCES[@]}"

# Verilator 実行
verilator \
  --cc \
  --exe \
  --trace \
  --Wno-PINCONNECTEMPTY \
  --Wno-DECLFILENAME \
  --Wno-UNUSED \
  --Wno-WIDTH \
  "${RTL_SOURCES[@]}" \
  "${SRC_DIR}/ikaopll_wrapper.cpp" \
  "${SRC_DIR}/ym2413_bus.c" \
  "${SRC_DIR}/vgm_player.c" \
  "${SRC_DIR}/wav_writer.c" \
  "${SRC_DIR}/main_vgm_csv.c" \
  -CFLAGS "-O2" \
  -o ikaopll_sim

# 生成された Makefile を使ってビルド
make -C obj_dir -f VIKAOPLL.mk ikaopll_sim

echo "Running simulation..."
cd "${ROOT_DIR}"
./build/obj_dir/ikaopll_sim

echo "Done."