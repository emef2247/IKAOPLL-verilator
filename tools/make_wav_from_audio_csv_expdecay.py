#!/usr/bin/env python3
"""
tools/make_wav_from_audio_csv_expdecay.py

CSV -> WAV converter that applies an exponential-decay (IIR) reconstruction
and optional lowpass / soft-clip postprocessing.

Defaults chosen from experiments:
  - tau_ms = 1.5
  - mo_gain = 1.0
  - acc_gain = 0.30
  - lpf_fc = 1500 Hz (optional)
  - softclip: off by default
  - normalize: on by default

Usage:
  python3 tools/make_wav_from_audio_csv_expdecay.py audio_samples.csv out.wav
    [--fs 44100] [--tau_ms 1.5] [--mo-gain 1.0] [--acc-gain 0.3]
    [--lpf-fc 1500] [--softclip] [--softclip-k 1.0] [--no-normalize]
"""
import argparse, csv, sys, math
import numpy as np
import soundfile as sf
from scipy.signal import butter, filtfilt

def read_csv(path):
    times = []
    mo_vals = []
    acc_vals = []
    with open(path, newline='') as fh:
        r = csv.reader(fh)
        hdr = next(r, None)
        for row in r:
            if not row or len(row) < 3:
                continue
            times.append(int(row[0]))
            mo_vals.append(int(row[1]))
            acc_vals.append(int(row[2]))
    return np.array(times, dtype=np.int64), np.array(mo_vals, dtype=np.int32), np.array(acc_vals, dtype=np.int32)

def exp_iir_reconstruct(mo_vals, acc_vals, fs, tau_ms, mo_gain, acc_gain):
    N = len(mo_vals)
    dt = 1.0 / fs
    tau_s = max(1e-9, tau_ms * 1e-3)
    alpha = math.exp(-dt / tau_s)
    one_minus = 1.0 - alpha
    mo_norm = mo_vals.astype(np.float64) / 512.0
    acc_norm = acc_vals.astype(np.float64) / 32768.0
    y = np.zeros(N, dtype=np.float64)
    prev = 0.0
    for i in range(N):
        inp = mo_gain * mo_norm[i] + acc_gain * acc_norm[i]
        prev = prev * alpha + one_minus * inp
        y[i] = prev
    return y

def lowpass(y, fs, fc):
    if fc is None:
        return y
    b, a = butter(2, fc / (fs / 2.0), btype='low')
    # filtfilt for zero-phase
    return filtfilt(b, a, y)

def soft_clip(y, k):
    if k <= 0:
        return y
    # tanh-based soft clip
    return np.tanh(k * y) / np.tanh(k)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv", help="Input CSV (t_ps,mo_signed,acc_signed)")
    p.add_argument("out", help="Output WAV file path (e.g. out.wav)")
    p.add_argument("--fs", type=int, default=44100)
    p.add_argument("--tau_ms", type=float, default=1.5)
    p.add_argument("--mo-gain", type=float, default=1.0)
    p.add_argument("--acc-gain", type=float, default=0.30)
    p.add_argument("--lpf-fc", type=float, default=1500.0,
                   help="Optional 2-pole Butterworth LPF cutoff (Hz). Default 1500")
    p.add_argument("--no-lpf", action="store_true", help="Disable LPF even if lpf-fc provided")
    p.add_argument("--softclip", action="store_true", help="Apply tanh soft clip after LPF")
    p.add_argument("--softclip-k", type=float, default=1.0, help="Softclip strength (k)")
    p.add_argument("--no-normalize", dest="normalize", action="store_false", help="Disable final normalization")
    args = p.parse_args()

    times, mo_vals, acc_vals = read_csv(args.csv)
    if len(mo_vals) == 0:
        print("No samples read from CSV.")
        sys.exit(1)

    y = exp_iir_reconstruct(mo_vals, acc_vals, args.fs, args.tau_ms, args.mo_gain, args.acc_gain)

    # Low-pass
    if not args.no_lpf and args.lpf_fc and args.lpf_fc > 0.0:
        y = lowpass(y, args.fs, args.lpf_fc)

    # Soft clip
    if args.softclip:
        y = soft_clip(y, args.softclip_k)

    # Normalize (unless disabled)
    if args.normalize:
        peak = np.max(np.abs(y))
        if peak > 0.0:
            target = 10**(-1.0/20.0)  # -1 dBFS
            y = y * (target / peak)

    # Clip and convert to int16
    y = np.clip(y, -1.0, 1.0)
    out_int16 = np.int16(y * 32767)
    sf.write(args.out, out_int16, args.fs, subtype='PCM_16')
    print("WAV written:", args.out, "samples:", len(out_int16), "fs:", args.fs)

if __name__ == "__main__":
    main()