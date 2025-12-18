#!/usr/bin/env python3
"""
VGM -> YM2413 CSV (delay,reg,data) converter

eseopl3patcher/main.c の while ループと、
vgm_helpers.c の record_csv() の挙動をトレースして、
YM2413(0x51) コマンド列を CSV (delay,reg,data) に落とします。

制限:
  - 時刻は wait コマンド (0x70–0x7F, 0x61, 0x62, 0x63) のみで進める。
  - YM2413 以外のレジスタ書き込みは「時間 0 の即時イベント」とみなし、
    timestamp は更新しない（C 実装と同じ）。
  - AY8910(0xA0), K051649(0xD2) は固定長テーブルでスキップ。
  - ループは 1 周目のみを対象とし、展開はしない。
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path
import csv
import sys


def read_le_u32(buf: bytes, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


def parse_vgm_header(data: bytes) -> tuple[int, int | None]:
    """
    C main.c と同じ:
      vgm_data_offset = *(0x34)
      if 0 → 0x0C
      data_start = 0x34 + vgm_data_offset
      orig_loop_offset = *(0x1C)
      orig_loop_address = (!=0xFFFFFFFF) ? (orig_loop_offset + 0x04) : 0
    """
    if len(data) < 0x40:
        raise ValueError("VGM header too small (< 0x40 bytes)")
    if data[0:4] != b"Vgm ":
        raise ValueError("Not a VGM file (missing 'Vgm ' header)")

    vgm_data_offset = read_le_u32(data, 0x34)
    if vgm_data_offset == 0:
        vgm_data_offset = 0x0C

    data_start = 0x34 + vgm_data_offset
    if data_start >= len(data):
        raise ValueError(f"data_start(0x{data_start:X}) beyond EOF({len(data):X})")

    orig_loop_offset = read_le_u32(data, 0x1C)
    if orig_loop_offset == 0xFFFFFFFF:
        loop_addr = None
    else:
        loop_addr = orig_loop_offset + 0x04

    return data_start, loop_addr


def vgm_to_ym2413_csv(vgm_path: Path, csv_path: Path) -> None:
    data = vgm_path.read_bytes()
    data_start, loop_addr = parse_vgm_header(data)
    end = len(data)

    pc = data_start

    # C の vgmctx.timestamp.current_sample / csv_last_sample に対応
    current_sample = 0
    csv_last_sample = 0

    # C 側 kKnownFixedCmds
    fixed_cmd_lengths = {
        0xA0: 3,  # AY8910
        0xD2: 4,  # K051649
    }

    with csv_path.open("w", newline="") as f_out:
        writer = csv.writer(f_out)
        writer.writerow(["delay", "reg", "data"])

        while pc < end:
            cmd = data[pc]

            # --- YM2413 write 0x51 rr vv ---
            if cmd == 0x51:
                if pc + 2 >= end:
                    print(f"[WARN] Truncated YM2413 at 0x{pc:X}, stop.", file=sys.stderr)
                    break
                reg = data[pc + 1]
                val = data[pc + 2]
                pc += 3

                # record_csv と同じロジック
                if csv_last_sample == 0:
                    delta = current_sample
                else:
                    delta = current_sample - csv_last_sample
                csv_last_sample = current_sample

                writer.writerow([delta, "01", f"{reg:02X}"])
                writer.writerow([0, "00", f"0x{val:02X}"])
                continue

            # --- YM3812 / YM3526 / Y8950 書き込み(3バイト) ---
            if cmd in (0x5A, 0x5B, 0x5C):
                if pc + 2 >= end:
                    print(f"[WARN] Truncated OPL cmd 0x{cmd:02X} at 0x{pc:X}, stop.", file=sys.stderr)
                    break
                pc += 3
                continue

            # --- OPN-family passthrough: 0x52/0x54/0x55/0x56/0x57 (3バイト) ---
            if cmd in (0x52, 0x54, 0x55, 0x56, 0x57):
                if pc + 2 >= end:
                    print(f"[WARN] Truncated OPN cmd 0x{cmd:02X} at 0x{pc:X}, stop.", file=sys.stderr)
                    break
                pc += 3
                continue

            # --- short wait: 0x70–0x7F ---
            if 0x70 <= cmd <= 0x7F:
                wait_samples = (cmd & 0x0F) + 1
                current_sample += wait_samples
                pc += 1
                continue

            # --- wait n samples: 0x61 ll hh ---
            if cmd == 0x61:
                if pc + 2 >= end:
                    print(f"[WARN] Truncated 0x61 at 0x{pc:X}, stop.", file=sys.stderr)
                    break
                lo = data[pc + 1]
                hi = data[pc + 2]
                ws = lo | (hi << 8)
                current_sample += ws
                pc += 3
                continue

            # --- wait 1/60s: 0x62 ---
            if cmd == 0x62:
                current_sample += 735
                pc += 1
                continue

            # --- wait 1/50s: 0x63 ---
            if cmd == 0x63:
                current_sample += 882
                pc += 1
                continue

            # --- End: 0x66 ---
            if cmd == 0x66:
                pc += 1
                break

            # --- AY8910 / K051649 (固定長テーブル) ---
            if cmd in fixed_cmd_lengths:
                length = fixed_cmd_lengths[cmd]
                if pc + (length - 1) >= end:
                    print(f"[WARN] Truncated fixed cmd 0x{cmd:02X} at 0x{pc:X}, stop.", file=sys.stderr)
                    break
                pc += length
                continue

            # --- その他 unknown: 1バイトだけ forward ---
            pc += 1


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Extract YM2413(0x51) register timeline from VGM into CSV (delay,reg,data)."
    )
    ap.add_argument("vgm", help="Input .vgm file")
    ap.add_argument(
        "-o", "--output",
        help="Output CSV path (default: <input>.vgm.csv)"
    )
    args = ap.parse_args(argv)

    vgm_path = Path(args.vgm)
    if not vgm_path.exists():
        print(f"[ERROR] No such file: {vgm_path}", file=sys.stderr)
        return 1

    if args.output:
        csv_path = Path(args.output)
    else:
        # 期待されている ym2413_scale_chromatic.vgm.csv 形式に合わせる
        csv_path = vgm_path.with_suffix(vgm_path.suffix + ".csv")

    try:
        vgm_to_ym2413_csv(vgm_path, csv_path)
    except Exception as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        return 1

    print(f"[INFO] Wrote CSV: {csv_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())