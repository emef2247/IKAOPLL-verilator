#!/usr/bin/env python3
"""
Join periods with registers, filter, and fit:
  f_obs ≈ a * (FNUM * 2^BLOCK) + b

Usage:
  python3 tools/join_and_fit_filtered.py \
    --periods vcd_extract_test/periods.csv \
    --fnum vcd_extract_test/o_FNUM.csv \
    --block vcd_extract_test/o_BLOCK.csv \
    --mul  vcd_extract_test/o_MUL.csv \
    --out  vcd_extract_test/joined_fit_filtered.csv \
    --fmin 50 --fmax 12000

Outputs fit coefficients and a small sample mapping (time,FNUM,BLOCK,obs,pred,note).
"""
import argparse, csv, os, glob, math
import numpy as np

def find_alternative(fn_pattern):
    d = os.path.dirname(fn_pattern) or '.'
    base = os.path.basename(fn_pattern).split('.')[0]
    cands = glob.glob(os.path.join(d, base + '__*.csv'))
    if not cands: return None
    cands.sort(key=lambda p: os.path.getsize(p), reverse=True)
    return cands[0]

def read_timeseries(fn):
    if not fn or not os.path.exists(fn):
        return np.array([]), np.array([])
    times=[]; vals=[]
    with open(fn,'r') as f:
        r = csv.reader(f); hdr = next(r, None)
        for row in r:
            if not row or len(row) < 2: continue
            try:
                t = float(row[0])
            except:
                continue
            s = row[1].strip()
            if s == '': continue
            if all(ch in '01' for ch in s):
                v = int(s,2)
            else:
                try:
                    v = int(s,0)
                except:
                    try:
                        v = int(float(s))
                    except:
                        v = 0
            times.append(t); vals.append(v)
    return np.array(times), np.array(vals)

def autodetect_periods(fn):
    with open(fn,'r') as f:
        r = csv.reader(f); hdr = next(r, None)
        if not hdr: raise RuntimeError("empty periods")
        hdr_l = [h.lower() for h in hdr]
        # prefer freq_hz and t_center_s columns
        tcol = None; fcol = None
        for i,h in enumerate(hdr_l):
            if tcol is None and ('t_center' in h or 'time' in h or 'wrap_time' in h):
                tcol = i
            if fcol is None and ('freq' in h or 'freq_hz' in h):
                fcol = i
        if tcol is None: tcol = 0
        if fcol is None: fcol = 1
    times=[]; freqs=[]
    with open(fn,'r') as f:
        r = csv.reader(f); next(r,None)
        for row in r:
            if not row: continue
            try:
                times.append(float(row[tcol]))
                freqs.append(float(row[fcol]))
            except:
                continue
    return np.array(times), np.array(freqs)

def make_lookup(times, vals):
    if len(times)==0:
        return lambda t: 0
    def lookup_seconds(t, times_arr=times, vals_arr=vals):
        # times_arr are register times in register units (maybe ps)
        # caller must convert secs->reg_units before calling
        idx = np.searchsorted(times_arr, t, side='right')-1
        if idx < 0:
            return int(vals_arr[0])
        return int(vals_arr[idx])
    return lookup_seconds

def freq_to_note(f):
    if f <= 0 or not np.isfinite(f): return ("-", -999)
    midi = 69 + 12 * math.log2(f/440.0)
    m_round = int(round(midi))
    note_names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
    name = note_names[m_round % 12] + str((m_round // 12) - 1)
    return (name, m_round)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--periods', required=True)
    p.add_argument('--fnum', required=True)
    p.add_argument('--block', required=True)
    p.add_argument('--mul', required=False)
    p.add_argument('--out', default='joined_fit_filtered.csv')
    p.add_argument('--fmin', type=float, default=50.0)
    p.add_argument('--fmax', type=float, default=12000.0)
    args = p.parse_args()

    t_periods, f_obs = autodetect_periods(args.periods)
    print("Loaded periods:", len(t_periods))

    fnum_file = args.fnum; block_file = args.block; mul_file = args.mul
    for name, path in (('fnum',fnum_file), ('block',block_file), ('mul',mul_file)):
        if path and (not os.path.exists(path) or os.path.getsize(path) <= 16):
            alt = find_alternative(path)
            if alt:
                print(f"Using alternative for {name}: {alt}")
                if name == 'fnum': fnum_file = alt
                if name == 'block': block_file = alt
                if name == 'mul': mul_file = alt

    f_times, f_vals = read_timeseries(fnum_file)
    b_times, b_vals = read_timeseries(block_file)
    m_times, m_vals = read_timeseries(mul_file) if mul_file else (np.array([]), np.array([]))

    print("Register counts: FNUM", len(f_times), "BLOCK", len(b_times), "MUL", len(m_times))

    # detect reg time units (ps if median > 1e6)
    reg_multiplier = 1.0
    if len(f_times) > 0 and np.median(f_times) > 1e6:
        reg_multiplier = 1e-12
    print("Register times assumed to be multiplied by", reg_multiplier, "to get seconds")

    # build lookups that accept seconds t and return value
    def lookup_from_arrays(times, vals):
        if len(times) == 0:
            return lambda t: 0
        def lookup_sec(t):
            # convert seconds -> reg_time units
            rt = t / reg_multiplier
            idx = np.searchsorted(times, rt, side='right') - 1
            if idx < 0:
                return int(vals[0])
            return int(vals[idx])
        return lookup_sec

    f_lookup = lookup_from_arrays(f_times, f_vals)
    b_lookup = lookup_from_arrays(b_times, b_vals)
    m_lookup = lookup_from_arrays(m_times, m_vals) if len(m_times)>0 else (lambda t: 0)

    # find first time when FNUM nonzero
    nonzero_idxs = np.where(f_vals != 0)[0]
    if len(nonzero_idxs) == 0:
        t_first_nonzero = None
        print("Warning: no non-zero FNUM in registers")
    else:
        first_idx = nonzero_idxs[0]
        t_first_nonzero = f_times[first_idx] * reg_multiplier
        print("First nonzero FNUM at reg-time index", first_idx, "-> sec", t_first_nonzero)

    X=[]; Y=[]; Ts=[]
    for t_s, fval in zip(t_periods, f_obs):
        # if FNUM not yet set, skip
        if t_first_nonzero is not None and t_s < t_first_nonzero:
            continue
        # filter freq range
        if not np.isfinite(fval): continue
        if abs(fval) < 1e-12: continue
        if abs(fval) < args.fmin or abs(fval) > args.fmax: continue
        F = f_lookup(t_s); B = b_lookup(t_s); M = m_lookup(t_s)
        Xv = F * (2**B)
        if Xv <= 0: continue
        X.append(Xv); Y.append(fval); Ts.append(t_s)
    X = np.array(X); Y = np.array(Y); Ts = np.array(Ts)
    print("After filtering: points:", len(X))

    if len(X) < 10:
        print("Not enough points for fit (after filtering).")
        return

    A = np.vstack([X, np.ones_like(X)]).T
    a,b = np.linalg.lstsq(A, Y, rcond=None)[0]
    print("Fit result: freq = a * X + b")
    print(" a =", a, " b =", b)

    # write joined file with predictions and note names
    with open(args.out, 'w', newline='') as fo:
        w = csv.writer(fo)
        w.writerow(['time_s','freq_obs','FNUM','BLOCK','MUL','X','pred_freq','note'])
        for t, obs, x in zip(Ts, Y, X):
            F = f_lookup(t); B = b_lookup(t); M = m_lookup(t)
            pred = a * x + b
            note, midi = freq_to_note(abs(pred))
            w.writerow([f"{t:.12e}", f"{obs:.6f}", int(F), int(B), int(M), int(x), f"{pred:.6f}", note])
    print("Wrote", args.out)

