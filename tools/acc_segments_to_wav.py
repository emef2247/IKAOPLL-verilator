#!/usr/bin/env python3
import csv
import numpy as np
from scipy.io import wavfile

SEG_CSV = "acc_segments.csv"
OUT_WAV = "acc_from_acc_segments.wav"
FS_OUT  = 44100.0  # 出力サンプリングレート

def main():
    segments = []
    with open(SEG_CSV, newline="") as f:
        r = csv.reader(f)
        header = next(r, None)  # t_start_s,t_end_s,duration_s,acc
        for row in r:
            if len(row) < 4:
                continue
            t0 = float(row[0])
            t1 = float(row[1])
            acc = int(row[3])
            segments.append((t0, t1, acc))

    if not segments:
        print("no segments")
        return

    # 総時間からサンプル数を決める
    t_end_total = segments[-1][1]
    n_samples = int(np.ceil(t_end_total * FS_OUT))
    print(f"total time ~ {t_end_total:.6f} s, samples = {n_samples}")

    buf = np.zeros(n_samples, dtype=np.float64)

    for t0, t1, acc in segments:
        if t1 <= t0:
            continue
        i0 = int(np.floor(t0 * FS_OUT))
        i1 = int(np.floor(t1 * FS_OUT))
        if i0 >= n_samples:
            continue
        if i1 > n_samples:
            i1 = n_samples
        if i1 <= i0:
            continue
        buf[i0:i1] = acc

    # 正規化して int16 へ
    peak = np.max(np.abs(buf))
    if peak == 0:
        print("all zeros; writing silent WAV")
        pcm = np.zeros_like(buf, dtype=np.int16)
    else:
        scale = 0.9 * 32767.0 / peak
        print(f"peak={peak}, scale={scale}")
        pcm = np.clip(buf * scale, -32768, 32767).astype(np.int16)

    wavfile.write(OUT_WAV, int(FS_OUT), pcm)
    print(f"wrote {OUT_WAV}")

if __name__ == "__main__":
    main()

