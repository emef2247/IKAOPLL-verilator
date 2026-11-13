#!/usr/bin/env python3
"""
tools/make_wav_from_mo_current.py

Usage:
  python3 tools/make_wav_from_mo_current.py mo_log.csv out.wav
  python3 tools/make_wav_from_mo_current.py mo_non1.csv out_non1.wav --fc 3000 --skip-baseline

Reads a CSV with header "t_ps,mo" or a similar two-column CSV.
By default it skips rows where mo == 1 (baseline). Use --include-baseline to keep them.

Algorithm:
  - Read events (t_ps, mo)
  - Convert mo (10-bit unsigned) to signed: if mo >= 512 then mo - 1024 else mo
  - Place impulses at event sample indices (value = mo_signed / 512.0)
  - Apply single-pole lowpass (RC) with cutoff fc (Hz)
  - Auto-normalize (or use --gain to multiply)
  - Write 16-bit PCM WAV

Requires: numpy, soundfile
  pip3 install numpy soundfile
"""
import argparse
import csv
import math
import numpy as np
import soundfile as sf
import sys
from statistics import median

def mo_to_signed(mo):
    mo = int(mo)
    if mo >= 512:
        return mo - 1024
    return mo

def read_events(path, skip_baseline=True):
    events = []
    with open(path, 'r') as f:
        r = csv.reader(f)
        header = next(r, None)
        # allow header either present or absent; assume first two columns are t_ps, mo
        for row in r:
            if not row or len(row) < 2:
                continue
            try:
                t_ps = int(row[0])
                mo = int(row[1])
            except:
                continue
            if skip_baseline and mo == 1:
                continue
            events.append((t_ps * 1e-12, mo))  # convert ps -> s
    return events

def make_wave(events, fs=44100, fc=4000.0, gain_arg=0.0, pad_tail_s=0.005):
    if not events:
        return None, None

    # stats
    t0 = events[0][0]
    t1 = events[-1][0]
    duration = t1 - t0 + pad_tail_s
    n_samples = max(1, int(duration * fs) + 1)
    times = np.arange(n_samples) / fs + t0

    imp = np.zeros(n_samples, dtype=np.float32)

    # place impulses (normalized by 512)
    for t, v in events:
        idx = int((t - t0) * fs)
        if idx < 0 or idx >= n_samples:
            continue
        val = mo_to_signed(v) / 512.0
        imp[idx] += val

    # compute some stats for feedback
    nonzero_imp = np.count_nonzero(imp)
    peak_imp = float(np.max(np.abs(imp))) if nonzero_imp else 0.0

    # single-pole LPF (RC)
    dt = 1.0 / fs
    RC = 1.0 / (2.0 * math.pi * fc)
    alpha = dt / (RC + dt)
    y = np.zeros_like(imp)
    y[0] = imp[0]
    for i in range(1, n_samples):
        y[i] = y[i-1] + alpha * (imp[i] - y[i-1])

    peak_after = float(np.max(np.abs(y))) if y.size else 0.0
    rms = float(np.sqrt(np.mean(y*y))) if y.size else 0.0

    # apply gain or auto-normalize to -1 dBFS
    if gain_arg != 0.0:
        y2 = y * gain_arg
        applied_gain = gain_arg
    else:
        if peak_after == 0:
            y2 = y
            applied_gain = 1.0
        else:
            target_db = -1.0
            target_lin = 10**(target_db / 20.0)
            applied_gain = target_lin / peak_after
            y2 = y * applied_gain

    # clip and to int16
    y2 = np.clip(y2, -1.0, 1.0)
    y16 = np.int16(y2 * 32767)

    stats = {
        'events': len(events),
        'duration_s': duration,
        'n_samples': n_samples,
        'imp_nonzero': int(nonzero_imp),
        'imp_peak': peak_imp,
        'peak_after_lp': peak_after,
        'rms_after_lp': rms,
        'applied_gain': applied_gain
    }

    return y16, stats

def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("out")
    p.add_argument("--fs", type=int, default=44100)
    p.add_argument("--fc", type=float, default=4000.0, help="LPF cutoff (Hz)")
    p.add_argument("--gain", type=float, default=0.0, help="linear gain to apply (overrides auto-normalize)")
    p.add_argument("--skip-baseline", action="store_true", default=True, help="skip mo==1 rows (default: True)")
    p.add_argument("--pad-tail-ms", type=float, default=5.0, help="pad tail in ms")
    args = p.parse_args()

    events = read_events(args.csv, skip_baseline=args.skip_baseline)
    if not events:
        print("No events read (maybe all mo==1 or file empty). Exiting.")
        sys.exit(1)

    # print simple stats
    times = [t for (t,_) in events]
    deltas = [ (times[i+1]-times[i]) for i in range(len(times)-1) ] if len(times)>1 else []
    print("Events:", len(events))
    if deltas:
        ds = sorted(deltas)
        med = median(ds)
        print("time span: {:.6f}s .. {:.6f}s (duration {:.6f}s)".format(times[0], times[-1], times[-1]-times[0]))
        print("delta (sec) min/median/max: {:.6e} / {:.6e} / {:.6e}".format(ds[0], med, ds[-1]))
    else:
        print("Single event only.")

    y16, stats = make_wave(events, fs=args.fs, fc=args.fc, gain_arg=args.gain, pad_tail_s=args.pad_tail_ms*1e-3)
    if y16 is None:
        print("No waveform generated.")
        sys.exit(1)

    print("Wave stats:", stats)
    sf.write(args.out, y16, args.fs, subtype='PCM_16')
    print("WAV written:", args.out)

if __name__ == "__main__":
    main()

