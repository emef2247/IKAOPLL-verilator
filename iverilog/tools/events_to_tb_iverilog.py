#!/usr/bin/env python3
"""
Convert tests/events_csv_tb.v (IKAOPLL_write_simple calls) to a Verilog initial
block calling write_addr_then_data(addr,data) pairs, suitable for tb_iverilog.v.

Usage:
  python3 tools/events_to_tb_iverilog.py tests/events_csv_tb.v > tests/events_for_tb_iverilog.v
"""
import sys
import re

if len(sys.argv) < 2:
    print("Usage: events_to_tb_iverilog.py <events_csv_tb.v>", file=sys.stderr)
    sys.exit(2)

path = sys.argv[1]
pat = re.compile(r"IKAOPLL_write_simple\(\s*([^,]+)\s*,\s*([^)\s;]+)\s*\)")

entries = []
with open(path, "r") as f:
    for ln in f:
        m = pat.search(ln)
        if m:
            a = m.group(1).strip()
            d = m.group(2).strip()
            # normalize a: it's either 1'b0 / 1'b1 or a hex. we want addr/data pairs,
            # so store as tuple (a_is_addr_flag, value)
            entries.append((a, d))

# Now pair entries: expect pattern addr(A0=0) then data(A0=1)
pairs = []
i = 0
while i < len(entries)-1:
    a_flag, a_val = entries[i]
    b_flag, b_val = entries[i+1]
    # If first entry has A0==0 (address), second should be A0==1 (data)
    if re.match(r"1'b?0", a_flag) or a_flag in ("0", "1'b0"):
        # assume a_val is address (8'h..) and b_val is data
        addr = a_val
        data = b_val
        pairs.append((addr, data))
        i += 2
    else:
        # fallback: if format unexpected, try to skip 1
        i += 1

# Emit Verilog initial block
print("// Generated events for tb_iverilog (converted from events_csv_tb.v)")
print("initial begin")
for addr, data in pairs:
    # ensure values are 8'hXX form; if not, try to leave as-is
    print(f"    write_addr_then_data({addr}, {data});")
    print("    #10;")  # small gap between transactions (adjust if needed)
print("    $display(\"[TB] events done\");")
print("    #100 $finish;")
print("end")