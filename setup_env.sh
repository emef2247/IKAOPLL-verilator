#!/usr/bin/env bash
# Prepare everything IKAOPLL-verilator needs, then prove it with a smoke render:
#   1. required tools (Verilator, make, C/C++ compilers, zlib headers, curl/wget)
#   2. the upstream IKAOPLL RTL in rtl/ (fetched, never committed)
#   3. the simulator build (build/obj_dir/ikaopll_sim)
#   4. one bundled fixture rendered to a temporary WAV
#
# Usage: ./setup_env.sh
# Safe to re-run: anything already present is left as-is.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${ROOT_DIR}"

missing_pkgs=()
need() { # need <command> <apt-package>
  if ! command -v "$1" >/dev/null 2>&1; then missing_pkgs+=("$2"); fi
}
need verilator verilator
need make build-essential
need g++ build-essential
need cc build-essential
need python3 python3
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
  missing_pkgs+=(curl)
fi
if [[ ! -f /usr/include/zlib.h ]]; then missing_pkgs+=(zlib1g-dev); fi

if [[ ${#missing_pkgs[@]} -gt 0 ]]; then
  # De-duplicate while keeping order.
  mapfile -t missing_pkgs < <(printf '%s\n' "${missing_pkgs[@]}" | awk '!seen[$0]++')
  echo "Missing tools; packages needed: ${missing_pkgs[*]}"
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y "${missing_pkgs[@]}"
  else
    echo "apt-get is not available. Install the packages above and re-run." >&2
    exit 1
  fi
fi
for cmd in verilator make g++ cc python3; do
  command -v "${cmd}" >/dev/null 2>&1 || { echo "Still missing: ${cmd}" >&2; exit 1; }
done
[[ -f /usr/include/zlib.h ]] || { echo "Still missing: zlib headers (zlib1g-dev)" >&2; exit 1; }
echo "Tools OK: $(verilator --version 2>&1 | head -1)"

if [[ ! -f rtl/IKAOPLL.v ]]; then
  echo "Fetching IKAOPLL RTL into rtl/ ..."
  bash tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/blob/main
fi
[[ -f rtl/IKAOPLL.v && -f rtl/IKAOPLL_LICENSE ]] || { echo "rtl/ is incomplete; see tools/create_rtl_dir.sh" >&2; exit 1; }
echo "RTL OK: rtl/IKAOPLL.v (+ IKAOPLL_modules, license in rtl/IKAOPLL_LICENSE)"

echo "Building simulator ..."
bash build_and_run.sh --build-only >/dev/null
[[ -x build/obj_dir/ikaopll_sim ]] || { echo "Build did not produce build/obj_dir/ikaopll_sim" >&2; exit 1; }
echo "Build OK: build/obj_dir/ikaopll_sim"

smoke_dir="$(mktemp -d)"
trap 'rm -rf "${smoke_dir}"' EXIT
build/obj_dir/ikaopll_sim tests/vgm/ym2413_scale_rom1.vgm --output "${smoke_dir}/scale.wav" >/dev/null
[[ -s "${smoke_dir}/scale.wav" ]] || { echo "Smoke render produced no WAV" >&2; exit 1; }
echo "Smoke OK: tests/vgm/ym2413_scale_rom1.vgm rendered"
echo "Setup complete. Batch-render with: python3 export_vgm.py <VGM-or-dir> --outdir outputs"
