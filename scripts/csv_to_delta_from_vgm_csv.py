#!/usr/bin/env python3
"""
csv_to_delta_from_vgm_csv.py

Convert the CSV you produced (absolute timestamp CSV with columns:
 timestamp,command_count,cmd_hex,byte1,byte2,...) into a delta CSV expected by the tb:
 delay_samples,reg_hex,data_hex

Usage:
  python3 csv_to_delta_from_vgm_csv.py input_abs.csv output_delta.csv

Notes:
- This script extracts only YM2413 writes (opcode 0x51) and writes their reg/data as rows.
- It assumes timestamp is in audio sample units (e.g. 44100 Hz base) as produced by vgm2txt.
"""
import sys
import csv

def parse_hex_token(tok):
    if tok is None:
        return None
    t = tok.strip()
    if t == '':
        return None
    # allow "0x.." or ".."
    try:
        return int(t, 16)
    except:
        try:
            if t.lower().startswith('0x'):
                return int(t, 16)
        except:
            return None
    return None

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 csv_to_delta_from_vgm_csv.py input_abs.csv output_delta.csv")
        return 2
    infile = sys.argv[1]
    outfile = sys.argv[2]

    events = []  # list of (timestamp, reg, data)

    with open(infile, newline='') as f:
        reader = csv.reader(f)
        # no strict header required — skip header-like line if first cell contains non-digit
        # We'll process every row that looks valid
        for row in reader:
            if not row:
                continue
            # Ensure at least 3 columns (timestamp, count, cmd)
            if len(row) < 3:
                continue
            ts_str = row[0].strip()
            # skip non-numeric header rows
            if ts_str == '' or (not (ts_str[0].isdigit() or (ts_str[0] == '-' and len(ts_str) > 1))):
                # try to sanitize header like 'timestamp_samples'
                lower0 = ts_str.lower()
                if 'timestamp' in lower0 or 'timestamp_samples' in lower0:
                    continue
                # otherwise try to parse (but if fails skip)
                try:
                    int(ts_str)
                except:
                    continue
            try:
                ts = int(float(ts_str))
            except:
                # skip unparsable
                continue
            # cmd is in column index 2 per your example (0x61 etc)
            cmd_tok = row[2].strip() if len(row) > 2 else ''
            # normalize cmd hex (could be "0x61" or "61")
            try:
                cmd = int(cmd_tok, 16) if cmd_tok.startswith('0x') or cmd_tok.startswith('0X') else int(cmd_tok, 16)
            except:
                # if cannot parse, skip
                continue

            # YM2413 write: opcode 0x51 ; payload bytes typically follow in cols 3 and 4
            if cmd == 0x51:
                b1 = None
                b2 = None
                if len(row) > 3:
                    b1 = parse_hex_token(row[3])
                if len(row) > 4:
                    b2 = parse_hex_token(row[4])
                # require both reg and data present
                if b1 is None or b2 is None:
                    # skip incomplete write
                    continue
                events.append((ts, b1 & 0xFF, b2 & 0xFF))

    if not events:
        print("No YM2413 (0x51) write events found in input.")
        return 1

    # sort by absolute timestamp to be safe
    events.sort(key=lambda e: e[0])

    # convert to delta CSV
    with open(outfile, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['delay_samples', 'reg_hex', 'data_hex'])
        prev_ts = 0
        for ts, reg, data in events:
            delta = ts - prev_ts
            if delta < 0:
                delta = 0
            writer.writerow([str(delta), f"{reg:02X}", f"{data:02X}"])
            prev_ts = ts

    print(f"Wrote {len(events)} YM2413 write events to {outfile}")
    return 0

if __name__ == '__main__':
    sys.exit(main())

