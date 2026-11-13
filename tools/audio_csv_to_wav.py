#!/usr/bin/env python3
# tools/audio_csv_to_wav.py
#
# Usage:
#   python3 tools/audio_csv_to_wav.py audio_samples.csv out.wav [--fs 44100] [--mo-gain 1.0] [--acc-gain 0.0]
#
# Expects CSV format: t_ps,mo_signed,acc_signed
# - mo_signed: integer (signed) from ikaopll_get_mo_signed() (roughly -512..+511)
# - acc_signed: integer (signed) from ikaopll_get_acc_signed() (roughly -32768..+32767)
#
# This script treats CSV as uniform-sampled in order (it ignores t_ps except for optional sanity checks),
# converts columns to float, mixes mo and acc with gains and writes 16-bit PCM WAV.
import argparse
import csv
import numpy as np
import soundfile as sf
import sys

def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("out")
    p.add_argument("--fs", type=int, default=44100)
    p.add_argument("--mo-gain", type=float, default=1.0)
    p.add_argument("--acc-gain", type=float, default=0.0)
    p.add_argument("--normalize", action="store_true", help="auto-normalize to -1 dB")
    args = p.parse_args()

    times = []
    mo_vals = []
    acc_vals = []
    with open(args.csv, newline='') as f:
        r = csv.reader(f)
        hdr = next(r, None)
        for row in r:
            if not row or len(row) < 3:
                continue
            tps = int(row[0])
            mo = int(row[1])
            acc = int(row[2])
            times.append(tps)
            mo_vals.append(mo)
            acc_vals.append(acc)

    if len(mo_vals) == 0:
        print("No samples read.")
        sys.exit(1)

    # Convert to float - scale
    mo_f = np.array(mo_vals, dtype=np.float64) / 512.0   # approx -1..+1
    acc_f = np.array(acc_vals, dtype=np.float64) / 32768.0

    mix = args.mo_gain * mo_f + args.acc_gain * acc_f

    # optional normalize
    if args.normalize:
        peak = np.max(np.abs(mix))
        if peak > 0:
            target = 10**(-1.0/20.0) # -1dB
            mix = mix * (target / peak)

    # clip and convert to int16
    mix = np.clip(mix, -1.0, 1.0)
    out = np.int16(mix * 32767)

    sf.write(args.out, out, args.fs, subtype='PCM_16')
    print("WAV written:", args.out)
    print("samples:", len(out), "fs:", args.fs)
    # print simple time-range (from CSV)
    print("t_ps first/last:", times[0], times[-1])

if __name__ == "__main__":
    main()

