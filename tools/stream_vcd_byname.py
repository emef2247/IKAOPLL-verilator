#!/usr/bin/env python3
"""
stream_vcd_byname.py

Stream-extract selected signals from a large VCD by name (suffix match allowed).
Lightweight and does not expand the whole VCD into memory.

Usage:
  python3 tools/stream_vcd_byname.py --vcd debug.vcd --out out.csv \
    --signals cyc18r_phase_sr_out cyc3r_phase_current o_DAC_OPDATA o_OP_PHASE fnum block mul \
    --start 1279489000000 --end 1279491000000
"""
import argparse, re, sys, csv

def parse_header(vcd_path):
    id2name = {}
    with open(vcd_path, 'r', errors='ignore') as f:
        for line in f:
            # remove leading whitespace so '$var' with indentation is matched
            line_stripped = line.lstrip()
            if line_stripped.startswith('$var'):
                toks = line_stripped.split()
                # $var <type> <size> <id> <reference...> $end
                if len(toks) >= 6:
                    idcode = toks[3]
                    # full reference text is toks[4:-1]; take first token as base name
                    fullref = ' '.join(toks[4:-1])
                    basename = fullref.split()[0]  # drop "[8:0]" or "(0)" etc.
                    id2name[idcode] = basename
            if line_stripped.startswith('$enddefinitions'):
                break
    return id2name

def resolve_ids(id2name, requested):
    # requested: list of names (suffix match allowed)
    # return map idcode->name for matched ones
    resolved = {}
    for idcode, name in id2name.items():
        for req in requested:
            if name == req or name.endswith(req):
                resolved[idcode] = name
                break
    return resolved

def stream_extract(vcd_path, out_csv, idmap, start=None, end=None):
    # idmap: dict idcode->name to capture
    ids_to_watch = set(idmap.keys())
    last_values = {idc: '' for idc in idmap}
    current_time = None
    pending_changes = {}
    # Write CSV header: time, name1, name2...
    names = [idmap[idc] for idc in idmap]
    with open(vcd_path, 'r', errors='ignore') as f, open(out_csv, 'w', newline='') as fo:
        w = csv.writer(fo)
        w.writerow(['time'] + names)
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if line.startswith('$enddefinitions'):
                # header ended; continue scanning
                continue
            if line.startswith('$dumpvars'):
                # skip initial dumpvars block handling for now, or could parse it
                continue
            if line.startswith('#'):
                t = int(line[1:])
                # write pending changes at previous time (if any) only if within window
                if current_time is not None and pending_changes:
                    if (start is None or current_time >= start) and (end is None or current_time <= end):
                        # apply pending to last_values
                        for idc,val in pending_changes.items():
                            last_values[idc] = val
                        row = [str(current_time)]
                        for idc in idmap:
                            row.append(last_values.get(idc, ''))
                        w.writerow(row)
                    pending_changes.clear()
                current_time = t
                if end is not None and current_time > end:
                    break
                continue
            # value change lines
            if line[0] in ('0','1'):
                val = line[0]
                idcode = line[1:].strip()
                if idcode in ids_to_watch:
                    pending_changes[idcode] = val
            elif line[0] in ('b','r'):
                parts = line.split()
                if len(parts) >= 2:
                    val = parts[0][1:]
                    idcode = parts[1]
                    if idcode in ids_to_watch:
                        pending_changes[idcode] = val
            else:
                # ignore other directives
                pass
        # flush final pending
        if current_time is not None and pending_changes:
            if (start is None or current_time >= start) and (end is None or current_time <= end):
                for idc,val in pending_changes.items():
                    last_values[idc] = val
                row = [str(current_time)]
                for idc in idmap:
                    row.append(last_values.get(idc, ''))
                w.writerow(row)
    return True

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--vcd', required=True)
    p.add_argument('--out', required=True)
    p.add_argument('--signals', nargs='+', required=True, help='names (basename OK) to extract')
    p.add_argument('--start', type=int, default=None)
    p.add_argument('--end', type=int, default=None)
    args = p.parse_args()

    print("Parsing header...")
    id2name = parse_header(args.vcd)
    print("Total signals in header:", len(id2name))
    idmap = resolve_ids(id2name, args.signals)
    if not idmap:
        print("No requested signals matched. Available signals (sample):")
        cnt = 0
        for idc,name in id2name.items():
            print(idc, name)
            cnt += 1
            if cnt >= 100:
                break
        sys.exit(2)
    print("Resolved the following ids:")
    for idc,name in idmap.items():
        print("  %s -> %s" % (idc, name))
    print("Streaming and extracting changes ...")
    ok = stream_extract(args.vcd, args.out, idmap, start=args.start, end=args.end)
    if ok:
        print("Wrote", args.out)

if __name__ == '__main__':
    main()

