#!/usr/bin/env python3
# analyze_preview.py
# Reads out_from_vgm.wav, prints stats, makes two preview files:
#  - preview_scaled.wav : fixed scale (multiply) clipped to int16
#  - preview_smoothed.wav: fixed scale + simple moving average smoothing
import wave, sys, numpy as np, math

IN = "out_from_vgm.wav"
SCALE = 128      # まずは 128 を試す。小さければ減らす、大きければ増やす。
SMOOTH_WINDOW = 5  # moving average window (odd)

def read_wav(fn):
    with wave.open(fn,"rb") as w:
        nch = w.getnchannels(); sr = w.getframerate(); nframes = w.getnframes(); sw = w.getsampwidth()
        data = w.readframes(nframes)
    if sw != 2:
        raise SystemExit("only 16-bit wav supported")
    arr = np.frombuffer(data, dtype=np.int16)
    if nch > 1:
        arr = arr.reshape(-1,nch)[:,0]
    return arr.astype(np.int32), sr

def write_wav(fn, arr, sr):
    arr16 = np.clip(np.round(arr), -32768, 32767).astype(np.int16)
    with wave.open(fn,"wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(arr16.tobytes())

a, sr = read_wav(IN)
n = len(a)
print("frames=", n, "sr=", sr)
nz = np.count_nonzero(a)
print("non-zero:", nz, "(", 100.0*nz/n, "% )")
print("min/max/mean/rms:", a.min(), a.max(), a.mean(), math.sqrt((a.astype(float)**2).mean()))
print("first 200 samples (as ints):", a[:200].tolist()[:200])

# Fixed scaling (interpret original as signed; scaling factor multiplies)
scaled = a.astype(np.int32) * SCALE
scaled = np.clip(scaled, -32768, 32767)
write_wav("preview_scaled.wav", scaled, sr)
print("Wrote preview_scaled.wav with SCALE=", SCALE)

# Simple smoothing (moving average) after scaling to reduce short spikes
k = SMOOTH_WINDOW
pad = k//2
padded = np.pad(scaled, (pad,pad), mode='edge')
kernel = np.ones(k)/k
smoothed = np.convolve(padded, kernel, mode='valid')
write_wav("preview_smoothed.wav", smoothed, sr)
print("Wrote preview_smoothed.wav with window=", k)
print("Done. Play preview_scaled.wav / preview_smoothed.wav (aplay/ffplay) to compare.")

