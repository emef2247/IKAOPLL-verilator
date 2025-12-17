#!/usr/bin/env python3
# save_wav_from_mo.py
# Usage: python3 save_wav_from_mo.py mo_log_ab_en.csv out.wav

import sys
import csv
import numpy as np
import math
import soundfile as sf   # pip install soundfile

if len(sys.argv) < 3:
    print("Usage: python3 save_wav_from_mo.py mo_log_ab_en.csv out.wav")
    sys.exit(1)

csv_path = sys.argv[1]
out_wav = sys.argv[2]
fs = 44100

# Read events: prefer B-side when dac_en_B==1, otherwise ignore (or use A)
events = []  # list of (t_sec, value_signed)
with open(csv_path, 'r') as f:
    r = csv.DictReader(f)
    for row in r:
        # fields: t_ps,dac_en_A,mo_A,dac_en_B,mo_B
        t_ps = int(row['t_ps'])
        dac_en_A = int(row['dac_en_A'])
        mo_A = int(row['mo_A'])
        dac_en_B = int(row['dac_en_B'])
        mo_B = int(row['mo_B'])

        # Decide which to use: prefer B if enabled, else if A enabled use A
        if dac_en_B:
            mo = mo_B
            t_sec = t_ps * 1e-12  # ps -> s
            events.append((t_sec, mo))
        elif dac_en_A:
            mo = mo_A
            t_sec = t_ps * 1e-12
            events.append((t_sec, mo))
        else:
            # skip zeros (no DAC_EN) to reduce size; optionally include if desired
            continue

# If no events, exit
if not events:
    print("No DAC events found (dac_en).")
    sys.exit(1)

# Convert mo (0..1023) to signed -512..+511
def mo_to_signed(mo):
    mo = int(mo)
    if mo >= 512:
        return mo - 1024
    else:
        return mo

# Build piecewise-constant signal sampled at fs
t_start = events[0][0]
t_end = events[-1][0] + 0.001  # pad 1ms tail
n_samples = int((t_end - t_start) * fs) + 1
out = np.zeros(n_samples, dtype=np.float32)
times = np.arange(n_samples) / fs + t_start

# Walk events and fill intervals
ei = 0
cur_t, cur_v = events[0]
cur_v_s = mo_to_signed(cur_v)
for i, tt in enumerate(times):
    # advance event index while next event time <= tt
    while ei+1 < len(events) and events[ei+1][0] <= tt:
        ei += 1
        cur_t, cur_v = events[ei]
        cur_v_s = mo_to_signed(cur_v)
    # assign: normalize by 512
    out[i] = float(cur_v_s) / 512.0

# Apply single-pole lowpass (RC) with cutoff fc (Hz)
fc = 20000.0  # try 20kHz per app note
dt = 1.0 / fs
RC = 1.0 / (2 * math.pi * fc)
alpha = dt / (RC + dt)
y = np.zeros_like(out)
y[0] = out[0]
for n in range(1, out.size):
    y[n] = y[n-1] + alpha * (out[n] - y[n-1])

# Scale to int16 and write
y = y * 0.9  # headroom
y16 = np.int16(np.clip(y, -1.0, 1.0) * 32767)
sf.write(out_wav, y16, fs, subtype='PCM_16')
print("WAV written:", out_wav)

