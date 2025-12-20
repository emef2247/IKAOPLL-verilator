#!/usr/bin/env python3
"""
mo_changes_to_wav_weighted.py

Convert a value-change CSV (t_ps,mo_signed) into a 16-bit mono WAV by
time-weighted averaging (proper decimation) per output sample.

Usage:
  python3 tools/mo_changes_to_wav_weighted.py -i mo_value_changes.csv -o mo_avg.wav --sr 44100 --scale 64

This computes for each audio sample interval [S,E):
  avg = (1/(E-S)) * integral_{t=S}^{E} v(t) dt
where v(t) is the piecewise-constant signal defined by the change events.
"""
import argparse, csv, struct, wave
from typing import List, Tuple

INT16_MIN = -32768
INT16_MAX = 32767

def clamp_int16(x: int) -> int:
    if x < INT16_MIN: return INT16_MIN
    if x > INT16_MAX: return INT16_MAX
    return x

def load_changes(path: str) -> List[Tuple[int, int]]:
    changes = []
    with open(path, newline='') as f:
        r = csv.reader(f)
        h = next(r, None)
        for row in r:
            if not row: continue
            t = int(row[0].strip())
            v = int(row[1].strip())
            changes.append((t, v))
    if not changes:
        raise RuntimeError("no events")
    return changes

def write_wav(path: str, samples, sr: int):
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(struct.pack('<' + 'h'*len(samples), *samples))

def decimate_weighted(changes, samplerate, start_ps=None, end_ps=None, scale=64.0):
    period_ps = 1e12 / float(samplerate)  # ps per sample
    if start_ps is None:
        start_ps = changes[0][0]
    if end_ps is None:
        end_ps = changes[-1][0]
    if end_ps <= start_ps:
        raise RuntimeError("end_ps must be > start_ps")

    # Build segments: list of (t_start, t_end, value)
    segs = []
    for i in range(len(changes)):
        t0, v0 = changes[i]
        t1 = changes[i+1][0] if i+1 < len(changes) else (end_ps + period_ps)  # extend beyond end
        segs.append((t0, t1, v0))

    # If first segment starts after start_ps, prepend a segment from -inf to first t with first value
    if segs[0][0] > start_ps:
        segs.insert(0, (start_ps, segs[0][0], segs[0][2]))
    # Now walk intervals
    n_samples = int((end_ps - start_ps) / period_ps) + 1
    samples = []
    seg_idx = 0
    for si in range(n_samples):
        S = start_ps + si * period_ps
        E = S + period_ps
        # accumulate time-weighted sum over segments overlapping [S,E)
        total = 0.0
        covered = 0.0
        # advance seg_idx if segment ends before S
        while seg_idx < len(segs) and segs[seg_idx][1] <= S:
            seg_idx += 1
        j = seg_idx
        while j < len(segs) and segs[j][0] < E:
            seg_s, seg_e, seg_v = segs[j]
            ov_s = max(S, seg_s)
            ov_e = min(E, seg_e)
            if ov_e > ov_s:
                dt = ov_e - ov_s
                total += seg_v * dt
                covered += dt
            j += 1
        # If not fully covered (e.g., no segments overlap), we can use last known value (extend previous)
        if covered < (E - S):
            # Find last value before S
            last_v = segs[seg_idx-1][2] if seg_idx > 0 else segs[0][2]
            missing = (E - S) - covered
            total += last_v * missing
            covered = E - S
        avg = total / (E - S)
        val = int(round(avg * scale))
        samples.append(clamp_int16(val))
    return samples

def main():
    p = argparse.ArgumentParser()
    p.add_argument('-i','--input', default='mo_value_changes.csv')
    p.add_argument('-o','--output', default='mo_avg.wav')
    p.add_argument('--sr', type=int, default=44100)
    p.add_argument('--scale', type=float, default=64.0)
    p.add_argument('--start-ps', type=int)
    p.add_argument('--end-ps', type=int)
    args = p.parse_args()

    changes = load_changes(args.input)
    start_ps = args.start_ps if args.start_ps else changes[0][0]
    end_ps = args.end_ps if args.end_ps else changes[-1][0]
    print(f"Loaded {len(changes)} events. Decimating {start_ps}..{end_ps} ps -> sr={args.sr}, scale={args.scale}")
    samples = decimate_weighted(changes, args.sr, start_ps=start_ps, end_ps=end_ps, scale=args.scale)
    print(f"Generated {len(samples)} samples ({len(samples)/args.sr:.3f} s). Writing {args.output}")
    write_wav(args.output, samples, args.sr)
    print("Done.")

if __name__ == '__main__':
    main()

