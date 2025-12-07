#!/usr/bin/env python3
# analyze_sparsity.py
# Reads out_from_vgm.wav and prints statistics about non-zero samples,
# writes CSV of non-zero clusters and (optionally) a PNG plot if matplotlib is available.
import wave, numpy as np, math, sys, csv, os

IN = "out_from_vgm.wav"
OUT_CLUSTERS = "nonzero_clusters.csv"
PLOT_PNG = "nonzero_density.png"

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

a, sr = read_wav(IN)
n = len(a)
nz = np.count_nonzero(a)
minv = int(a.min()); maxv = int(a.max())
rms = math.sqrt((a.astype(float)**2).mean())
print(f"frames={n} sr={sr} non-zero={nz} ({100.0*nz/n:.2f}%) min={minv} max={maxv} rms={rms:.1f}")

# find runs of non-zero samples
clusters = []
i = 0
while i < n:
    if a[i] != 0:
        start = i
        ssum = 0
        cnt = 0
        maxv_local = a[i]
        while i < n and a[i] != 0:
            ssum += a[i]
            cnt += 1
            if a[i] > maxv_local: maxv_local = a[i]
            i += 1
        clusters.append((start, cnt, int(maxv_local), int(ssum/cnt)))
    else:
        i += 1

print(f"clusters: {len(clusters)} (nonzero samples {nz})")
# print basic histogram of cluster lengths
lens = [c[1] for c in clusters]
if lens:
    import statistics
    print("cluster_len: min", min(lens), "median", statistics.median(lens), "mean", statistics.mean(lens), "max", max(lens))
else:
    print("no clusters found")

# write clusters to CSV (start_idx, length, peak, mean)
with open(OUT_CLUSTERS, "w", newline='') as f:
    w = csv.writer(f)
    w.writerow(["start_idx","length","peak","mean"])
    for c in clusters:
        w.writerow(c)
print("Wrote", OUT_CLUSTERS)

# produce a coarse density plot if matplotlib available
try:
    import matplotlib.pyplot as plt
    window = max(1, n // 2000)  # compress to ~2000 points
    density = np.array([np.count_nonzero(a[i:i+window]) for i in range(0,n,window)])
    t = np.arange(len(density)) * (window / sr)
    plt.figure(figsize=(10,4))
    plt.plot(t, density, '-', lw=0.7)
    plt.xlabel("time (s)")
    plt.ylabel("non-zero samples per window")
    plt.title("Non-zero sample density")
    plt.tight_layout()
    plt.savefig(PLOT_PNG, dpi=150)
    print("Wrote plot:", PLOT_PNG)
except Exception as e:
    print("matplotlib not available or failed to plot:", e)

