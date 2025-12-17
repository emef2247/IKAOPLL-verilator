#!/usr/bin/env python3
# tools/save_wav_from_mo_impulse.py
# Usage:
#   python3 tools/save_wav_from_mo_impulse.py mo_log_ab_en.csv out_impulse.wav --fs 44100 --fc 4000 --gain 1.0

import sys, csv, argparse, math
import numpy as np
import soundfile as sf

p = argparse.ArgumentParser()
p.add_argument("csv")
p.add_argument("out")
p.add_argument("--fs", type=int, default=44100)
p.add_argument("--fc", type=float, default=4000.0)
p.add_argument("--gain", type=float, default=1.0)
p.add_argument("--useB", action="store_true", help="prefer B-side (negedge) values when available")
args = p.parse_args()

def mo_to_signed(mo):
    mo = int(mo)
    if mo >= 512:
        return mo - 1024
    return mo

# read events (use B if available and enabled)
events = []
with open(args.csv, 'r') as f:
    r = csv.DictReader(f)
    for row in r:
        t_ps = int(row['t_ps'])
        dac_en_A = int(row['dac_en_A'])
        mo_A = int(row['mo_A'])
        dac_en_B = int(row['dac_en_B'])
        mo_B = int(row['mo_B'])

        # prefer B when useB True and enabled
        if args.useB:
            if dac_en_B:
                events.append((t_ps * 1e-12, mo_to_signed(mo_B)))
            elif dac_en_A:
                events.append((t_ps * 1e-12, mo_to_signed(mo_A)))
        else:
            # prefer B if enabled else A
            if dac_en_B:
                events.append((t_ps * 1e-12, mo_to_signed(mo_B)))
            elif dac_en_A:
                events.append((t_ps * 1e-12, mo_to_signed(mo_A)))

if not events:
    print("No events found.")
    sys.exit(1)

# stats
vals = np.array([v for (_,v) in events], dtype=np.int32)
print("Events:", len(events))
print("Time span:", events[0][0], "..", events[-1][0])
print("mo signed range:", vals.min(), vals.max())

fs = args.fs
t_start = events[0][0]
t_end = events[-1][0] + 0.01
n = int((t_end - t_start) * fs) + 1
print("Output samples:", n, "=>", n/fs, "s")
imp = np.zeros(n, dtype=np.float32)

# place impulses (normalized by 512)
for t, v in events:
    idx = int((t - t_start) * fs)
    if 0 <= idx < n:
        imp[idx] += (v / 512.0)

# Simple single-pole lowpass (RC) integrator
fc = args.fc
dt = 1.0 / fs
RC = 1.0 / (2.0 * math.pi * fc)
alpha = dt / (RC + dt)
y = np.zeros_like(imp)
y[0] = imp[0]
for i in range(1, n):
    y[i] = y[i-1] + alpha * (imp[i] - y[i-1])

# apply gain and normalize conservatively
y = y * args.gain
peak = np.max(np.abs(y))
if peak > 0:
    norm_gain = 0.95 / peak
    y *= norm_gain
    print("Applied normalization gain:", norm_gain)

y16 = np.int16(np.clip(y, -1.0, 1.0) * 32767)
sf.write(args.out, y16, fs, subtype='PCM_16')
print("WAV written:", args.out, "peak:", np.max(np.abs(y)))

