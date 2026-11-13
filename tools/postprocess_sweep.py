#!/usr/bin/env python3
"""
tools/postprocess_sweep.py

Produces combinations of postprocessed WAVs for quick comparison.

Now supports optional soft-clip and writes the low-frequency peak for each file.
"""
import os, sys, csv, math, argparse
import numpy as np
import soundfile as sf
from scipy.signal import butter, filtfilt

def read_csv(path):
    t=[]; mo=[]; acc=[]
    with open(path) as fh:
        r=csv.reader(fh)
        next(r,None)
        for row in r:
            if not row: continue
            t.append(int(row[0])); mo.append(int(row[1])); acc.append(int(row[2]))
    return np.array(t), np.array(mo,dtype=float), np.array(acc,dtype=float)

def exp_smooth(mo, acc, fs, tau_ms, mo_gain, acc_gain):
    N=len(mo)
    dt=1.0/fs
    tau = max(1e-9, tau_ms*1e-3)
    alpha = math.exp(-dt/tau)
    one_minus = 1.0-alpha
    y=np.zeros(N, dtype=float)
    prev=0.0
    mo_norm = mo / 512.0
    acc_norm = acc / 32768.0
    for i in range(N):
        inp = mo_gain * mo_norm[i] + acc_gain * acc_norm[i]
        prev = prev*alpha + one_minus*inp
        y[i]=prev
    return y

def lowpass(y, fs, fc):
    b,a = butter(2, fc/(fs/2), btype='low')
    return filtfilt(b,a,y)

def soft_clip(y, k):
    if k <= 0:
        return y
    return np.tanh(k * y) / np.tanh(k)

def find_low_peak(y, fs, fmin=50, fmax=2000):
    N = min(65536, len(y))
    x = y[:N] * np.hanning(N)
    spec = np.abs(np.fft.rfft(x))
    freqs = np.fft.rfftfreq(N, 1.0/fs)
    idx = np.where((freqs>=fmin)&(freqs<=fmax))[0]
    if idx.size==0:
        return None, None
    sub = spec[idx]
    im = np.argmax(sub)
    return freqs[idx[im]], 20*np.log10(sub[im]+1e-20)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("outdir")
    p.add_argument("--fs", type=int, default=44100)
    p.add_argument("--softclip-k", type=float, default=1.0)
    args=p.parse_args()

    t, mo, acc = read_csv(args.csv)
    taus = [1.5, 3.0, 6.0]        # ms
    acc_gains = [0.0, 0.1, 0.3]
    lpf_fcs = [None, 1000, 1500]  # Hz
    softclip_opts = [False, True]

    os.makedirs(args.outdir, exist_ok=True)
    for tau in taus:
        for ag in acc_gains:
            y_base = exp_smooth(mo, acc, args.fs, tau, 1.0, ag)
            for fc in lpf_fcs:
                y_lp = y_base.copy()
                if fc is not None:
                    y_lp = lowpass(y_lp, args.fs, fc)
                for sc in softclip_opts:
                    y2 = y_lp.copy()
                    if sc:
                        y2 = soft_clip(y2, args.softclip_k)
                    # normalize modestly
                    peak = np.max(np.abs(y2))
                    if peak>0:
                        y2 = y2 * (0.98/peak)
                    outname = f"exp_tau{tau:.1f}_acc{ag:.2f}"
                    if fc is not None:
                        outname += f"_lpf{fc}"
                    if sc:
                        outname += f"_softclip"
                    fname = os.path.join(args.outdir, outname+".wav")
                    sf.write(fname, (np.clip(y2,-1,1)*32767).astype('int16'), args.fs)
                    fpeak, db = find_low_peak(y2, args.fs)
                    print(outname, "->", fname, " low-peak:", fpeak, "Hz", f"{db:.1f}dB")
    print("done")

if __name__=="__main__":
    main()