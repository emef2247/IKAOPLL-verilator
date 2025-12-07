#!/usr/bin/env python3
import wave, sys, struct, math, numpy as np
if len(sys.argv) < 2:
    print("Usage: python3 analyze_wav.py out_from_vgm.wav")
    sys.exit(1)
fn = sys.argv[1]
with wave.open(fn, "rb") as w:
    nch = w.getnchannels()
    sr = w.getframerate()
    nframes = w.getnframes()
    sampwidth = w.getsampwidth()
    data = w.readframes(nframes)
fmt = '<' + {1:'b',2:'h',4:'i'}[sampwidth] * (nframes * nch)
arr = np.frombuffer(data, dtype=np.int16) if sampwidth==2 else np.frombuffer(data, dtype=np.int32)
if nch > 1:
    arr = arr.reshape(-1, nch)[:,0]   # take first channel
arrf = arr.astype(np.float64)
minv = arrf.min()
maxv = arrf.max()
mean = arrf.mean()
rms = math.sqrt((arrf**2).mean())
print(f"frames={nframes} channels={nch} sr={sr} sampwidth={sampwidth}")
print(f"min={minv} max={maxv} mean={mean:.3f} rms={rms:.3f}")
print("first 100 samples:", arrf[:100].tolist()[:100])
# show percent of non-zero samples
nz = (arrf != 0).sum()
print(f"non-zero samples: {nz}/{nframes} ({100.0*nz/nframes:.2f}%)")

