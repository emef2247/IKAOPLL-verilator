#!/usr/bin/env python3
# overlay_events.py
# Reads ym2413_scale_chromatic.delta.csv and out_from_vgm.wav, computes event sample indices
# and prints first/last events and how many events fall into non-zero clusters.
import csv, math, sys, numpy as np, wave

CSV = "ym2413_scale_chromatic.delta.csv"
WAV = "out_from_vgm.wav"
EMUCLK = 3579545.0
AUDIO = 44100.0

def read_events(fn):
    ev=[]
    with open(fn) as f:
        next(f)
        cum=0
        for row in csv.reader(f):
            if not row or row[0]=="":
                continue
            d=int(row[0]); reg=row[2] if len(row)>2 else ""; data=row[3] if len(row)>3 else ""
            cum += d
            ev.append((cum, reg, data))
    return ev

def read_wav_nonzero(fn):
    with wave.open(fn,"rb") as w:
        nch = w.getnchannels(); sr = w.getframerate(); nframes = w.getnframes()
        data = w.readframes(nframes)
    arr = np.frombuffer(data, dtype=np.int16)
    if nch>1:
        arr = arr.reshape(-1,nch)[:,0]
    nz = (arr != 0)
    return nz, sr, len(arr)

events = read_events(CSV)
nz, sr, nframes = read_wav_nonzero(WAV)
iter_per_sample = EMUCLK*2.0/AUDIO

print("events:", len(events))
print("iter_per_sample:", iter_per_sample)
mapped=[]
for cum,reg,data in events:
    tick = int(round(cum * iter_per_sample))
    sample_idx = int(round((tick) / iter_per_sample))  # the sample index corresponding to event
    mapped.append((cum, reg, data, tick, sample_idx))
print("first 10 mapped events (sample_idx, tick, reg, data):")
for m in mapped[:10]:
    print(m[3], m[4], m[1], m[2])
print("last 10 mapped events:")
for m in mapped[-10:]:
    print(m[3], m[4], m[1], m[2])

# For each event, check whether sample_idx +/- window has any non-zero
window = 4
hits=0
for (_,_,_,_tick, sample_idx) in mapped:
    low = max(0, sample_idx-window); high = min(nframes-1, sample_idx+window)
    if np.any(nz[low:high+1]):
        hits += 1
print(f"events that hit nonzero region within +/-{window} samples: {hits}/{len(mapped)}")
# Print list of event sample_idx for those that hit
print("Event sample indices that hit non-zero (first 20):")
cnt=0
for (cum,reg,data,tick,sample_idx) in mapped:
    low = max(0, sample_idx-window); high = min(nframes-1, sample_idx+window)
    if np.any(nz[low:high+1]):
        print(sample_idx, reg, data)
        cnt+=1
        if cnt>=20: break

