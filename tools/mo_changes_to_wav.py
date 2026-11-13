
#!/usr/bin/env python3
"""
mo_changes_to_wav.py

Convert a value-change CSV (t_ps,mo_signed) produced by ikaopll_mo_change_log
into a 16-bit mono WAV file by zero-order hold resampling.

Usage:
  python3 tools/mo_changes_to_wav.py -i mo_value_changes.csv -o mo.wav --samplerate 44100 --scale 64

Options:
  -i/--input       input CSV (default: mo_value_changes.csv)
  -o/--output      output WAV (default: mo.wav)
  --samplerate     sample rate in Hz (default: 44100)
  --scale          multiplicative scale applied to mo_signed before clipping to int16 (default: 64)
  --start-ps       optional start time (ps) to begin sampling (default: first event time)
  --end-ps         optional end time (ps) to stop sampling (default: last event time)
"""
import argparse
import csv
import struct
import wave
import sys
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
        header = next(r, None)
        # Accept header "t_ps,mo_signed" or no header
        for row in r:
            if not row: continue
            # Some CSVs may have whitespace
            t = int(row[0].strip())
            v = int(row[1].strip())
            changes.append((t, v))
    if not changes:
        raise RuntimeError("no change events loaded")
    return changes

def write_wav(path: str, samples: List[int], samplerate: int):
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(samplerate)
        # pack to little-endian signed 16
        frames = struct.pack('<' + 'h'*len(samples), *samples) if samples else b''
        wf.writeframes(frames)

def resample_zoh(changes: List[Tuple[int,int]], samplerate: int,
                 start_ps: int=None, end_ps: int=None, scale: float=64.0):
    # ps per sample
    period_ps = 1e12 / float(samplerate)  # 1 second = 1e12 ps
    if start_ps is None:
        start_ps = changes[0][0]
    if end_ps is None:
        end_ps = changes[-1][0]
    if end_ps <= start_ps:
        raise RuntimeError("end_ps must be > start_ps")
    # number of samples
    n_samples = int((end_ps - start_ps) / period_ps) + 1
    samples = [0] * n_samples

    ci = 0
    cur_t, cur_v = changes[0]
    # If first change is after start, keep its value starting at first change.
    # We'll treat cur_v as value from cur_t onwards.
    # Advance change index until the first change >= start_ps
    while ci + 1 < len(changes) and changes[ci+1][0] <= start_ps:
        ci += 1
        cur_t, cur_v = changes[ci]

    # index of next change
    next_index = ci + 1

    # For each sample index, compute sample_time_ps and set value accordingly.
    # We advance next_index when sample_time >= changes[next_index].t
    for si in range(n_samples):
        sample_time_ps = start_ps + si * period_ps
        # advance change pointer if next change has occurred
        while next_index < len(changes) and sample_time_ps >= changes[next_index][0]:
            cur_t, cur_v = changes[next_index]
            next_index += 1
        val = int(round(cur_v * scale))
        samples[si] = clamp_int16(val)
    return samples

def main():
    p = argparse.ArgumentParser()
    p.add_argument("-i","--input", default="mo_value_changes.csv")
    p.add_argument("-o","--output", default="mo.wav")
    p.add_argument("--samplerate", type=int, default=44100)
    p.add_argument("--scale", type=float, default=64.0)
    p.add_argument("--start-ps", type=int, help="start time in ps")
    p.add_argument("--end-ps", type=int, help="end time in ps")
    args = p.parse_args()

    changes = load_changes(args.input)
    start_ps = args.start_ps if args.start_ps is not None else changes[0][0]
    end_ps = args.end_ps if args.end_ps is not None else changes[-1][0]
    print(f"Loaded {len(changes)} change events. Sampling from {start_ps} ps to {end_ps} ps at {args.samplerate} Hz, scale={args.scale}")
    samples = resample_zoh(changes, args.samplerate, start_ps=start_ps, end_ps=end_ps, scale=args.scale)
    print(f"Generated {len(samples)} samples ({len(samples)/args.samplerate:.3f} s). Writing {args.output} ...")
    write_wav(args.output, samples, args.samplerate)
    print("Done.")

if __name__ == "__main__":
    main()
