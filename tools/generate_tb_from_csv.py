#!/usr/bin/env python3
"""
generate_tb_from_csv.py

Usage:
  python3 generate_tb_from_csv.py events.csv > tb_from_csv.sv

Produces a SystemVerilog initial-block fragment that issues IKAOPLL_write calls
matching the style in src/IKAOPLL_tb.sv. By default the script will try to
auto-detect ADDR/DATA marker usage, pair addresses with subsequent data and
emit SV calls with an inter-gap between address and data.

Options:
  --addr-marker <n>   : force treat marker value <n> as address marker
  --data-marker <n>   : force treat marker value <n> as data marker
  --inter-gap <n>     : # delay inserted between addr write and data write (default 100)
  --scale <f>         : multiply CSV delay units by <f> to produce SV # delays
  --max-scan <n>      : number of lines to scan for auto-detect (default 80)
"""
import sys
import argparse
import csv
import math

def parse_num(tok):
    tok = tok.strip()
    if tok == "":
        return 0
    try:
        if tok.startswith("0x") or tok.startswith("0X"):
            return int(tok,16)
        # detect hex w/out 0x if contains A-F
        if any(c in tok for c in "ABCDEFabcdef"):
            return int(tok,16)
        return int(tok,10)
    except:
        return 0

def autodetect_markers(lines, max_scan=80):
    # lines: list of CSV rows (list of cols)
    count0 = count1 = 0
    score0 = score1 = 0
    scanned = 0
    for r in lines:
        if scanned >= max_scan: break
        scanned += 1
        if len(r) < 3: continue
        marker = parse_num(r[1])
        payload = parse_num(r[2])
        if marker == 0: count0 += 1
        if marker == 1: count1 += 1
        if payload <= 0x3F:
            if marker == 0: score0 += 1
            if marker == 1: score1 += 1
    # decide
    if scanned == 0:
        return 0,1
    if score0 != score1:
        if score0 > score1: return 0,1
        else: return 1,0
    if count0 != count1:
        if count0 > count1: return 0,1
        else: return 1,0
    # fallback
    return 0,1

def build_events(rows, addr_marker, data_marker):
    events = []
    cum = 0
    have_addr = False
    pending_addr = 0
    pending_addr_tick = 0
    have_data = False
    pending_data = 0
    pending_data_tick = 0
    for r in rows:
        # expect at least 3 columns: delay, marker, payload
        if len(r) < 3: continue
        delay = parse_num(r[0])
        marker = parse_num(r[1])
        payload = parse_num(r[2])
        cum += delay
        if marker == addr_marker:
            # if we have a pending data -> create event using current cum as data time
            if have_data:
                events.append((cum, pending_addr, pending_data))
                have_data = False
            else:
                have_addr = True
                pending_addr = payload
                pending_addr_tick = cum
        elif marker == data_marker:
            if have_addr:
                events.append((pending_addr_tick, pending_addr, payload))
                have_addr = False
            else:
                have_data = True
                pending_data = payload
                pending_data_tick = cum
        else:
            # heuristics: payload top bits indicate addr if <=0x3F
            if payload <= 0x3F:
                if have_data:
                    events.append((cum, payload, pending_data))
                    have_data = False
                else:
                    have_addr = True
                    pending_addr = payload
                    pending_addr_tick = cum
            else:
                if have_addr:
                    events.append((pending_addr_tick, pending_addr, payload))
                    have_addr = False
                else:
                    have_data = True
                    pending_data = payload
                    pending_data_tick = cum
    # finalize
    if have_addr and have_data:
        events.append((pending_addr_tick, pending_addr, pending_data))
    return events

def emit_sv(events, inter_gap, scale):
    # events: list of (tick, reg, data)
    # We'll emit `#<delay> IKAOPLL_write(1'b0, 8'hRR, phiMref, CS_n, WR_n, A0, DIN);`
    out = []
    out.append("// Generated initial fragment from CSV")
    out.append("initial begin")
    prev_tick = 0.0
    for (t, regv, datav) in events:
        sv_delay = int(round((t - prev_tick) * scale))
        if sv_delay < 0: sv_delay = 0
        out.append(f"    #{sv_delay} IKAOPLL_write(1'b0, 8'h{regv:02X}, phiMref, CS_n, WR_n, A0, DIN);")
        out.append(f"    #{inter_gap} IKAOPLL_write(1'b1, 8'h{datav:02X}, phiMref, CS_n, WR_n, A0, DIN);")
        prev_tick = t
    out.append("end")
    return "\n".join(out)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", help="events CSV file")
    parser.add_argument("--addr-marker", type=int, help="force addr marker value (0/1)")
    parser.add_argument("--data-marker", type=int, help="force data marker value (0/1)")
    parser.add_argument("--inter-gap", type=int, default=100, help="gap between addr and data writes (SV # units). Default 100")
    parser.add_argument("--scale", type=float, default=1.0, help="scale CSV delay units to SV # units (default 1.0)")
    parser.add_argument("--max-scan", type=int, default=80, help="max lines to scan for autodetect")
    args = parser.parse_args()

    with open(args.csv, newline='') as f:
        reader = csv.reader(f)
        rows = [row for row in reader if any(c.strip() for c in row)]
    # autodetect markers if not provided
    if args.addr_marker is None or args.data_marker is None:
        adr, dat = autodetect_markers(rows[:args.max_scan], max_scan=args.max_scan)
        if args.addr_marker is None: addr_marker = adr
        else: addr_marker = args.addr_marker
        if args.data_marker is None: data_marker = dat
        else: data_marker = args.data_marker
    else:
        addr_marker = args.addr_marker
        data_marker = args.data_marker

    events = build_events(rows, addr_marker, data_marker)
    if not events:
        print("// No events detected in CSV", file=sys.stderr)
        sys.exit(1)
    sv = emit_sv(events, args.inter_gap, args.scale)
    print(sv)

if __name__ == "__main__":
    main()