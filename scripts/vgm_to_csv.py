#!/usr/bin/env python3
"""
vgm_to_csv.py
Strict VGM -> CSV extractor for YM2413 writes (accepts only 0x51).

CSV format:
delay_samples,reg_hex,data_hex

- delay_samples: number of audio samples to wait BEFORE performing the write (delta)
- reg_hex / data_hex: two-digit hex (uppercase)

Usage:
  python3 vgm_to_csv.py input.vgm output.csv
"""
import sys
import struct
import os

if len(sys.argv) < 3:
    print("Usage: python3 vgm_to_csv.py input.vgm output.csv")
    sys.exit(2)

inpath = sys.argv[1]
outpath = sys.argv[2]

if not os.path.isfile(inpath):
    print(f"Error: input file not found: {inpath}")
    sys.exit(1)

with open(inpath, "rb") as f:
    data = f.read()

if len(data) < 4 or data[:4] != b'Vgm ':
    print("Not a VGM file (missing 'Vgm ' header).")
    sys.exit(1)

# Compute data start offset (per VGM spec)
data_offset_field = 0
if len(data) >= 0x38:
    data_offset_field = struct.unpack_from("<I", data, 0x34)[0]
if data_offset_field != 0:
    pos = 0x34 + data_offset_field
else:
    pos = 0x40
if pos >= len(data):
    print(f"Warning: computed data offset {pos:#x} >= file length {len(data):#x}; aborting")
    sys.exit(1)

out_lines = []
pending_delay = 0  # in audio samples (delta)

def append_write(delay, reg, val):
    out_lines.append((delay, reg, val))

while pos < len(data):
    cmd = data[pos]
    pos += 1
    # End of data
    if cmd == 0x66:
        break
    # YM2413 write (strict): 0x51 only
    elif cmd == 0x51:
        if pos + 1 > len(data):
            print("Truncated YM2413 write at end of file.")
            break
        reg = data[pos]; val = data[pos+1]; pos += 2
        append_write(pending_delay, reg, val)
        pending_delay = 0
    elif cmd == 0x61:
        if pos + 1 > len(data):
            print("Truncated 0x61 wait; aborting.")
            break
        wait = struct.unpack_from("<H", data, pos)[0]; pos += 2
        pending_delay += wait
    elif cmd == 0x62:
        # wait 60 Hz frame = 735 samples at 44100 Hz
        pending_delay += 735
    elif cmd == 0x63:
        # wait 50 Hz frame = 882 samples at 44100 Hz
        pending_delay += 882
    elif 0x70 <= cmd <= 0x7F:
        pending_delay += (cmd & 0x0F) + 1
    elif cmd == 0x67:
        # data block - skip by length (32-bit LE)
        if pos + 4 > len(data):
            print("Truncated 0x67 block header; aborting.")
            break
        length = struct.unpack_from("<I", data, pos)[0]; pos += 4
        pos += length
    else:
        # Known small commands handling (best effort), otherwise warn & stop to avoid misparsing
        if cmd in (0x4F, 0x50):  # PSG single data byte
            if pos + 1 <= len(data):
                pos += 1
            else:
                break
        elif cmd in (0x52, 0x53, 0x55):  # other YM writes: skip two bytes (best effort)
            if pos + 2 <= len(data):
                pos += 2
            else:
                break
        else:
            print(f"Warning: unknown VGM command 0x{cmd:02X} at pos {pos-1:#x}; stopping parse.")
            break

# Write CSV (delta delays)
with open(outpath, "w") as f:
    f.write("delay_samples,reg_hex,data_hex\n")
    for delay, reg, val in out_lines:
        f.write(f"{delay},{reg:02X},{val:02X}\n")

print(f"Wrote {len(out_lines)} events to {outpath}")

