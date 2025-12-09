#!/usr/bin/env python3
"""
Convert tests/events_csv_tb.v (IKAOPLL_write_simple calls) into a statements-only file
with write_addr_then_data(addr, data); and small #gap between them.

Usage:
  python3 tools/events_to_tb_iverilog_full.py tests/events_csv_tb.v > tests/events_for_tb_iverilog.statements.v
"""
import sys
import re

if len(sys.argv) < 2:
    print("Usage: events_to_tb_iverilog_full.py <events_csv_tb.v>", file=sys.stderr)
    sys.exit(2)

path = sys.argv[1]
pat = re.compile(r"IKAOPLL_write_simple\(\s*([^,]+)\s*,\s*([^)\s;]+)\s*\)")

vals = []
with open(path, 'r') as f:
    for ln in f:
        m = pat.search(ln)
        if m:
            a = m.group(1).strip()
            d = m.group(2).strip()
            vals.append((a,d))

# Now pair address/data: find pairs where the first has A0=0 (1'b0) and next is A0=1 (1'b1)
pairs = []
i = 0
while i < len(vals)-1:
    a_flag, a_val = vals[i]
    b_flag, b_val = vals[i+1]
    # a_flag is the A0 flag string, but in our CSV->calls it's literally "1'b0" or "1'b1"
    if a_flag.startswith("1'b0") or a_flag.startswith("0") or a_flag == "1'b0":
        # treat a_val as addr, next as data (regardless of b_flag)
        pairs.append((a_val, b_val))
        i += 2
    else:
        # If out-of-order, advance one and try to resync
        i += 1

# Print statements-only file
print("// Generated statements-only file from", path)
for addr, data in pairs:
    print(f"    write_addr_then_data({addr}, {data});")
    print("    #10;")
# Do not emit $finish or initial; tb will finish.

