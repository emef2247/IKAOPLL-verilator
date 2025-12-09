#!/usr/bin/env python3
"""
Convert tests/events_csv_tb.v (relative #N delays using a timescale of 10ps)
into a statements-only file for tb_iverilog that:
 - pairs address/data events (A0=0 then A0=1) into write_addr_then_data(addr,data) calls
 - aligns each transaction to the nearest audio sample boundary for a given sample rate
 - emits delays in ns (suitable for a tb with `timescale 1ns/1ps`)

Behaviour:
 - Input lines expected like:
     #<N> IKAOPLL_write_simple(1'b0, 8'h0E);
   where the leading #<N> is a relative delay expressed in the origin timescale units.
 - The origin timescale default is 10 ps per unit (i.e., N * 10ps).
 - We accumulate relative delays into an absolute time (in ns), then align each transaction time
   to nearest sample boundary for the given sample rate (default 44100 Hz).
 - Output is a statements-only file that contains only `#<delay_ns>;` and
   `write_addr_then_data(8'hAA, 8'hBB);` lines (no initial/end/$finish).
 - Example usage:
     python3 tools/events_to_tb_iverilog_samplerate.py \
       --input tests/events_csv_tb.v \
       --output tests/events_for_tb_iverilog.statements.v \
       --sample-rate 44100

Notes:
 - The script forces a minimum inter-transaction delay of 1 ns to avoid 0-delay weirdness.
 - If an address entry is not followed by a data entry, that incomplete pair is skipped (with a warning).
"""

from __future__ import annotations
import argparse
import re
import math
import sys
from pathlib import Path

PAT = re.compile(r'^\s*#\s*([0-9]+)\s+IKAOPLL_write_simple\(\s*([^,]+)\s*,\s*([^)\s;]+)\s*\)\s*;')

def parse_events(path: Path, origin_unit_ps: float):
    """
    Parse events file and return a list of (time_ns, a0_flag_str, value_str) in chronological order.
    origin_unit_ps: number of picoseconds per input unit (e.g. 10 for 10ps units).
    """
    entries = []
    abs_units = 0  # in origin units (each unit = origin_unit_ps ps)
    with path.open('r') as f:
        for ln_no, ln in enumerate(f, start=1):
            m = PAT.match(ln)
            if not m:
                continue
            rel_N = int(m.group(1))
            a0_flag = m.group(2).strip()
            val = m.group(3).strip()
            abs_units += rel_N
            # convert to ns: abs_units * origin_unit_ps (ps) -> ns = /1000
            time_ns = abs_units * origin_unit_ps / 1000.0
            entries.append((time_ns, a0_flag, val, ln_no))
    return entries

def pair_entries(entries):
    """
    Pair address/data entries. We assume an address entry has a0_flag like "1'b0"
    and data entry is "1'b1". Pairs are formed by taking an addr entry and the next
    data entry after it. Returns list of (addr_time_ns, data_time_ns, addr_str, data_str).
    """
    pairs = []
    i = 0
    while i < len(entries)-1:
        time_a, flag_a, val_a, ln_a = entries[i]
        time_b, flag_b, val_b, ln_b = entries[i+1]
        # Accept if first looks like address (1'b0 or 0) and next like data (1'b1 or 1)
        if (flag_a.startswith("1'b0") or flag_a == "1'b0" or flag_a == "0") and \
           (flag_b.startswith("1'b1") or flag_b == "1'b1" or flag_b == "1"):
            pairs.append((time_a, time_b, val_a, val_b, ln_a, ln_b))
            i += 2
        else:
            # not a matching pair, advance by one and try to resync
            i += 1
    return pairs

def convert_to_aligned_statements(pairs, sample_rate_hz, min_delay_ns=1):
    """
    Convert paired (addr_time_ns, data_time_ns, addr_str, data_str, ...) into a list of
    statements with delays aligned to the nearest sample boundary.
    Returns list of (delay_ns_int, addr_str, data_str) where delay is the delay before the call
    relative to the previous output time.
    """
    sample_period_ns = 1e9 / sample_rate_hz  # ns per sample
    out = []
    prev_aligned_ns = 0.0
    for (t_addr, t_data, addr, data, ln_a, ln_b) in pairs:
        # choose transaction timestamp: use data time (so the data write is when we consider transaction complete)
        t_tx = max(t_addr, t_data)
        # compute nearest sample index and aligned time (ns)
        sample_idx = int(round(t_tx / sample_period_ns))
        aligned_ns = sample_idx * sample_period_ns
        # ensure non-decreasing aligned times
        if aligned_ns < prev_aligned_ns:
            # snap to prev_aligned_ns (or prev + min_delay)
            aligned_ns = prev_aligned_ns
        # compute relative delay from previous aligned
        rel_delay = aligned_ns - prev_aligned_ns
        rel_delay_int = int(round(rel_delay))
        if rel_delay_int < min_delay_ns:
            rel_delay_int = min_delay_ns
            aligned_ns = prev_aligned_ns + rel_delay_int
        out.append((rel_delay_int, addr, data, ln_a, ln_b, aligned_ns))
        prev_aligned_ns = aligned_ns
    return out

def format_addr_data(val_str: str) -> str:
    """
    Normalize the value string to a Verilog sized literal if possible.
    If val_str is like 8'h0E or 0x0E or decimal, attempt to convert to 8'hXX form.
    Otherwise return val_str as-is.
    """
    v = val_str.strip()
    # already in 8'hxx or similar
    if re.match(r"^[0-9]+'[bhd][0-9A-Fa-f_x]+$", v):
        return v
    # hex like 0xNN
    m = re.match(r"^0x([0-9A-Fa-f]+)$", v)
    if m:
        hx = m.group(1)
        return f"8'h{hx}"
    # plain hex with h prefix like h0E or 'h0E
    m2 = re.match(r"^h?([0-9A-Fa-f]+)$", v)
    if m2 and len(m2.group(1)) <= 2:
        return f"8'h{m2.group(1)}"
    # decimal
    if re.match(r"^\d+$", v):
        dec = int(v)
        return f"8'd{dec}"
    # fallback
    return v

def main():
    p = argparse.ArgumentParser(description="Convert events_csv_tb.v -> statements-only tb file aligned to sample-rate")
    p.add_argument('--input', '-i', type=Path, default=Path('tests/events_csv_tb.v'),
                   help='input events file (default: tests/events_csv_tb.v)')
    p.add_argument('--output', '-o', type=Path, default=Path('tests/events_for_tb_iverilog.statements.v'),
                   help='output statements-only file (default: tests/events_for_tb_iverilog.statements.v)')
    p.add_argument('--sample-rate', '-r', type=float, default=44100.0,
                   help='audio sample rate in Hz (default: 44100)')
    p.add_argument('--origin-unit-ps', type=float, default=10.0,
                   help='origin timescale unit in picoseconds (default: 10 for 10ps units)')
    p.add_argument('--min-delay-ns', type=int, default=1,
                   help='minimum emitted delay in ns between statements (default: 1)')
    args = p.parse_args()

    if not args.input.exists():
        print(f"Input file {args.input} not found.", file=sys.stderr)
        sys.exit(2)

    entries = parse_events(args.input, origin_unit_ps=args.origin_unit_ps)
    if not entries:
        print("No entries parsed from input. Exiting.", file=sys.stderr)
        sys.exit(2)

    pairs = pair_entries(entries)
    if not pairs:
        print("No address/data pairs found. Exiting.", file=sys.stderr)
        sys.exit(2)

    aligned = convert_to_aligned_statements(pairs, sample_rate_hz=args.sample_rate, min_delay_ns=args.min_delay_ns)

    # Write output file
    with args.output.open('w') as out:
        out.write("// Generated statements-only file (aligned to sample rate)\n")
        out.write(f"// input: {args.input}, sample_rate={args.sample_rate} Hz, origin_unit={args.origin_unit_ps} ps\n\n")
        for delay_ns, addr, data, ln_a, ln_b, aligned_ns in aligned:
            addr_fmt = format_addr_data(addr)
            data_fmt = format_addr_data(data)
            out.write(f"    #{delay_ns};\n")
            out.write(f"    write_addr_then_data({addr_fmt}, {data_fmt});\n")
        out.write("\n// end (tb will handle final $finish)\n")

    print(f"Wrote {len(aligned)} transactions to {args.output}")

if __name__ == '__main__':
    main()

