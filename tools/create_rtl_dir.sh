cat > tools/create_rtl_dir.sh <<'EOF'
#!/usr/bin/env bash
# Create rtl/ structure and populate IKAOPLL sources.
# Usage:
#   ./create_rtl_dir.sh [--from-dir PATH] [--fetch-raw GITHUB_BLOB_BASE] [--force]
#
# When using --fetch-raw, supply e.g.:
#   https://github.com/ika-musume/IKAOPLL/blob/main
# The script converts it to raw.githubusercontent.com/.../main internally,
# downloads LICENSE -> rtl/IKAOPLL_LICENSE and src/* files -> rtl/.
#
set -eu
progname=$(basename "$0")

print_usage() {
  cat <<EOF
$progname - prepare rtl/ directory for IKAOPLL sources

Usage:
  $progname [--from-dir PATH] [--fetch-raw GITHUB_BLOB_BASE] [--force] [--help]

Options:
  --from-dir PATH     Copy files from a local directory.
  --fetch-raw URL     Provide GitHub blob base, e.g.
                      https://github.com/ika-musume/IKAOPLL/blob/main
                      (script will translate to the corresponding raw URLs)
  --force             Overwrite existing files in rtl/.
  --help              Show this message.
EOF
}

TOP_FILES=(
  "IKAOPLL.v"
  "IKAOPLL_tb.sv"
)
MODULES_DIR="IKAOPLL_modules"
MODULE_FILES=(
  "IKAOPLL_dac.v"
  "IKAOPLL_eg.v"
  "IKAOPLL_lfo.v"
  "IKAOPLL_op.v"
  "IKAOPLL_pg.v"
  "IKAOPLL_primitives.v"
  "IKAOPLL_reg.v"
  "IKAOPLL_timinggen.v"
)

FROM_DIR=""
FETCH_RAW=""
FORCE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --from-dir)
      shift
      FROM_DIR="$1"
      ;;
    --fetch-raw)
      shift
      FETCH_RAW="$1"
      ;;
    --force)
      FORCE=1
      ;;
    --help|-h)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
      print_usage
      exit 2
      ;;
  esac
  shift
done

have_cmd() { command -v "$1" >/dev/null 2>&1; }

mkdir -p rtl
mkdir -p "rtl/${MODULES_DIR}"

copy_file_local() {
  local src="$1"; local dst="$2"
  if [[ ! -f "$src" ]]; then
    echo "ERROR: source file not found: $src" >&2; return 1
  fi
  if [[ -e "$dst" && $FORCE -eq 0 ]]; then
    echo "Skipping existing: $dst"; return 0
  fi
  cp -p "$src" "$dst"
  echo "Copied: $src -> $dst"
}

download_file() {
  local url="$1"; local dst="$2"
  if [[ -e "$dst" && $FORCE -eq 0 ]]; then
    echo "Skipping existing: $dst"; return 0
  fi
  if have_cmd curl; then
    if ! curl -fsSL -o "$dst" "$url"; then
      echo "ERROR: download failed: $url" >&2; return 1
    fi
  elif have_cmd wget; then
    if ! wget -q -O "$dst" "$url"; then
      echo "ERROR: download failed: $url" >&2; return 1
    fi
  else
    echo "ERROR: neither curl nor wget found; cannot download $url" >&2; return 2
  fi
  echo "Downloaded: $url -> $dst"
}

# If fetch-raw provided and looks like github blob URL, convert it to raw base
# Example input: https://github.com/ika-musume/IKAOPLL/blob/main
to_raw_base() {
  local blob="$1"
  # If contains github.com and /blob/, transform:
  if [[ "$blob" =~ github.com/ ]] && [[ "$blob" =~ /blob/ ]]; then
    # replace "https://github.com/" -> "https://raw.githubusercontent.com/"
    # and replace "/blob/<branch>" by "/<branch>"
    # e.g. https://github.com/user/repo/blob/main -> https://raw.githubusercontent.com/user/repo/main
    local raw
    raw=$(echo "$blob" | sed -e 's#^https://github.com/#https://raw.githubusercontent.com/#' -e 's#/blob/#/#')
    echo "$raw"
  else
    # assume already a raw base
    echo "$blob"
  fi
}

if [[ -n "$FROM_DIR" && -n "$FETCH_RAW" ]]; then
  echo "ERROR: specify only one of --from-dir or --fetch-raw" >&2
  exit 2
fi

if [[ -n "$FROM_DIR" ]]; then
  for f in "${TOP_FILES[@]}"; do
    copy_file_local "$FROM_DIR/$f" "rtl/$f" || exit 1
  done
  for f in "${MODULE_FILES[@]}"; do
    copy_file_local "$FROM_DIR/$MODULES_DIR/$f" "rtl/${MODULES_DIR}/$f" || exit 1
  done
  echo "Completed: files copied from $FROM_DIR to ./rtl/"
  exit 0
fi

if [[ -n "$FETCH_RAW" ]]; then
  raw_base=$(to_raw_base "$FETCH_RAW")
  # Download upstream LICENSE into rtl/
  download_file "${raw_base}/LICENSE" "rtl/IKAOPLL_LICENSE" || { echo "Failed to get upstream LICENSE"; exit 1; }

  # Now download sources under src/
  src_base="${raw_base}/src"
  for f in "${TOP_FILES[@]}"; do
    download_file "${src_base}/${f}" "rtl/${f}" || echo "Warning: failed to download ${src_base}/${f}"
  done
  for f in "${MODULE_FILES[@]}"; do
    download_file "${src_base}/${MODULES_DIR}/${f}" "rtl/${MODULES_DIR}/${f}" || echo "Warning: failed to download ${src_base}/${MODULES_DIR}/${f}"
  done

  echo "Completed: files downloaded from $FETCH_RAW to ./rtl/"
  exit 0
fi

echo "No --from-dir or --fetch-raw specified. Created rtl/ structure (empty)."
echo "You can populate it with:"
echo "  $progname --from-dir /path/to/IKAOPLL-sources"
echo "  $progname --fetch-raw https://github.com/owner/repo/blob/branch"
exit 0
EOF

chmod +x tools/create_rtl_dir.sh