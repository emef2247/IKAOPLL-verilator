#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

TB_DIR="${ROOT_DIR}/tb"
RTL_DIR="${ROOT_DIR}/rtl"
SRC_DIR="${ROOT_DIR}/src"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# RTL sources list
RTL_SOURCES=(
  "${RTL_DIR}/IKAOPLL.v"
)

# Add all .v under IKAOPLL_modules
while IFS= read -r -d '' f; do
  RTL_SOURCES+=("$f")
done < <(find "${RTL_DIR}/IKAOPLL_modules" -type f -name '*.v' -print0)

echo "RTL sources:"
printf '  %s\n' "${RTL_SOURCES[@]}"

# Verilator invocation
# Add -DHAVE_VERILATED_FST_C and Verilator include path so FST support can be compiled in.
# If your Verilator include path is different, adjust the -I option accordingly.
VERILATOR_INCLUDE="/usr/share/verilator/include"
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
  "${SRC_DIR}/vgm_parser.c" \
  "${SRC_DIR}/wav_writer.c" \
  "${SRC_DIR}/main_vgm_csv.c" \
  -CFLAGS "-O3 -march=native -flto -DHAVE_VERILATED_FST_C -I${VERILATOR_INCLUDE}" \
  -o ikaopll_sim

# Build using generated Makefile
make -C obj_dir -f VIKAOPLL.mk ikaopll_sim

echo "Running simulation..."
cd "${ROOT_DIR}"

# runtime args: first positional arg is csv/vgm path
SIM_INPUT=${1:-tests/csv/ym2413_scale_chromatic.vgm.csv}
# forward any extra runtime flags after the input path
shift 1 || true

# --- Determine runtime flags for informational display ---
# Default: do NOT produce CSV unless explicitly requested.
enable_trace=false
trace_file="ikaopll_dump.vcd"
trace_vcd="vcd"   # vcd 

enable_csv=false   # <-- default: disabled (equivalent to --no-csv)
enable_debug=false
debug_file="ym2413_bus_calls.log"

# Parse remaining args ($@) to detect trace flags, csv enable/disable and debug (with optional filename)
for arg in "$@"; do
  case "$arg" in
    --vcd)
      enable_trace=true
      trace_vcd="vcd"
      ;;
    --no-csv)
      enable_csv=false
      ;;
    --enable-csv)
      enable_csv=true
      ;;
    --debug)
      enable_debug=true
      ;;
    *)
      # ignore other runtime flags here (they'll be forwarded)
      ;;
  esac
done

# Compute input type for display (VGM if .vgm extension, else CSV)
input_type="CSV"
if [[ "${SIM_INPUT,,}" == *.vgm ]]; then
  input_type="VGM"
fi

# Print header info
echo "[run] Invoking simulator (trace build). Use --vcd to enable tracing."
echo "IKAOPLL-verilator: YM2413 bus + VGM CSV player"
printf "  INPUT: %s\n" "${SIM_INPUT}"
printf "  Input type: %s\n" "${input_type}"
if [ "${enable_trace}" = true ]; then
  printf "  Trace: enabled (format=%s) -> %s\n" "${trace_vcd}" "${trace_file}"
else
  printf "  Trace: disabled\n"
fi
printf "  CSV logging: %s\n" "$( [ "${enable_csv}" = true ] && echo "enabled" || echo "disabled (default)" )"
printf "  Debug log: %s\n" "$( [ "${enable_debug}" = true ] && echo "enabled -> ${debug_file}" || echo "disabled" )"

# Prepare output files
SIM_OUT="${BUILD_DIR}/sim_last_output.txt"
REAL_TIME_FILE="${BUILD_DIR}/sim_last_real_seconds.txt"
TIME_CMD_OUT="${BUILD_DIR}/sim_time_cmd.txt"

# Build the arguments to forward to the simulator.
# Forward original runtime args except --enable-csv / --no-csv (we control CSV default here).
FORWARD_ARGS=()
for a in "$@"; do
  case "$a" in
    --enable-csv|--no-csv)
      # skip, handled by this wrapper
      ;;
    *)
      FORWARD_ARGS+=("$a")
      ;;
  esac
done

# Build final simulator args: input + CSV flag (if disabled add --no-csv) + forwarded args
SIM_ARGS=("${SIM_INPUT}")
if [ "${enable_csv}" = false ]; then
  SIM_ARGS+=("--no-csv")
fi
SIM_ARGS+=("${FORWARD_ARGS[@]}")

# Informational print of the exact invocation
echo "[run] Executing: ${BUILD_DIR}/obj_dir/ikaopll_sim ${SIM_ARGS[*]}"

# Start high-resolution timestamp
start_ts=$(date +%s.%N)

# If /usr/bin/time is available, use it to capture real/user/sys in TIME_CMD_OUT,
# but still capture stdout/stderr of simulator to SIM_OUT.
if [ -x "$(command -v /usr/bin/time)" ]; then
  /usr/bin/time -p -o "${TIME_CMD_OUT}" "${BUILD_DIR}/obj_dir/ikaopll_sim" "${SIM_ARGS[@]}" > "${SIM_OUT}" 2>&1
  ret=$?
else
  "${BUILD_DIR}/obj_dir/ikaopll_sim" "${SIM_ARGS[@]}" > "${SIM_OUT}" 2>&1
  ret=$?
fi

# End timestamp
end_ts=$(date +%s.%N)

# Compute elapsed real seconds (high-resolution, in seconds)
real_seconds=$(awk "BEGIN {print ${end_ts} - ${start_ts}}")

# Save real seconds
printf "%f\n" "${real_seconds}" > "${REAL_TIME_FILE}"

# Parse /usr/bin/time output if available
time_real=""
time_user=""
time_sys=""
if [ -f "${TIME_CMD_OUT}" ]; then
  time_real=$(grep '^real' "${TIME_CMD_OUT}" | awk '{print $2}' || true)
  time_user=$(grep '^user' "${TIME_CMD_OUT}" | awk '{print $2}' || true)
  time_sys=$(grep '^sys' "${TIME_CMD_OUT}" | awk '{print $2}' || true)
fi

# Try to extract simulation time in seconds from simulator output.
# Expected line format:
# Simulation finished. sim_time = 6528880097280 ticks (6.528880 s)
sim_line=$(grep -m1 "Simulation finished" "${SIM_OUT}" || true)
sim_seconds=0
if [ -n "${sim_line}" ]; then
  # Extract the content inside parentheses and strip trailing ' s' if present
  sim_seconds=$(echo "${sim_line}" | awk -F'[()]' '{ if (NF>=2) {print $2} }' | sed 's/ s$//')
  # If extraction failed or not numeric, fallback to 0
  if ! echo "${sim_seconds}" | awk '{exit (!($0+0>0))}' 2>/dev/null; then
    sim_seconds=0
  fi
fi

# Compute Real Time Factor (RTF = elapsed_real / sim_seconds) if sim_seconds > 0
if [ "$(echo "${sim_seconds} > 0" | bc -l)" -eq 1 ]; then
  rtf=$(awk "BEGIN {printf \"%.6f\", ${real_seconds} / ${sim_seconds}}")
else
  rtf="N/A"
fi

# Print summary
printf "\n[run] Summary:\n"
printf "  Real elapsed time (date): %.6f s\n" "${real_seconds}"
if [ -n "${time_real}" ]; then
  printf "  time(1) output: real=%s s, user=%s s, sys=%s s (from /usr/bin/time)\n" "${time_real}" "${time_user:-N/A}" "${time_sys:-N/A}"
else
  printf "  time(1) output: (not available on this system)\n"
fi
if [ "${sim_seconds}" != "0" ]; then
  printf "  Simulation time   : %s s\n" "${sim_seconds}"
else
  printf "  Simulation time   : (not found in simulator output)\n"
fi
printf "  Real Time Factor (RTF = elapsed_real / sim_time): %s\n" "${rtf}"
printf "  Simulator exit status: %d\n" "${ret}"
printf "  Simulator stdout/stderr captured in: %s\n\n" "${SIM_OUT}"

exit ${ret}
