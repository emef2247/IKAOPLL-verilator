#!/usr/bin/env python3
"""
Convert mo_log.csv (t_ps,mo) into a WAV by:
 - placing impulses at event times on a high-rate grid (hi_sr)
 - low-pass filtering (simple RC IIR)
 - downsampling to out_sr (e.g. 44100) and writing 16-bit WAV

Usage:
  python3 tools/mo_log_to_wav.py mo_log.csv out.wav
Options:
  --hi-sr N         : high-rate sampling rate in Hz (default 1000000)
  --out-sr N        : output sample rate (default 44100)
  --cutoff F        : low-pass cutoff frequency in Hz (default 5000)
  --scale S         : multiply mo values by S before filtering (default 1.0)
  --pulse-width-us  : if given, use rectangular pulses of this width in microseconds (default: impulse)
"""
import sys, csv, wave, struct, argparse
import math
import numpy as np

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("inpath")
    p.add_argument("outpath")
    p.add_argument("--hi-sr", type=int, default=1000000)
    p.add_argument("--out-sr", type=int, default=44100)
    p.add_argument("--cutoff", type=float, default=5000.0)
    p.add_argument("--scale", type=float, default=1.0)
    p.add_argument("--pulse-width-us", type=float, default=0.0)
    return p.parse_args()

def clamp_int16(a):
    return max(-32768, min(32767, int(round(a))))

def read_mo_log(path, max_events=None):
    times_ps = []
    vals = []
    with open(path, 'r') as f:
        hdr = f.readline()
        for i, line in enumerate(f):
            if not line.strip(): continue
            parts = line.strip().split(',')
            if len(parts) < 2: continue
            t_ps = int(parts[0])
            v = int(parts[1])
            times_ps.append(t_ps)
            vals.append(v)
            if max_events and i+1 >= max_events:
                break
    return np.array(times_ps, dtype=np.int64), np.array(vals, dtype=np.float64)

def rc_iir_filter(x, sr, fc):
    # Single-pole low-pass (exponential smoothing)
    # alpha = dt / (RC + dt), RC = 1/(2*pi*fc)
    dt = 1.0 / sr
    RC = 1.0 / (2.0 * math.pi * fc)
    alpha = dt / (RC + dt)
    y = np.zeros_like(x, dtype=np.float64)
    s = 0.0
    for i in range(len(x)):
        s = s + alpha * (x[i] - s)
        y[i] = s
    return y

def main():
    args = parse_args()
    times_ps, vals = read_mo_log(args.inpath)
    if len(times_ps) == 0:
        print("No events found")
        return

    # convert times to seconds relative to first event
    t0 = times_ps[0]
    times_s = (times_ps - t0) * 1e-12

    hi_sr = args.hi_sr
    out_sr = args.out_sr
    fc = args.cutoff
    scale = args.scale
    pulse_w_us = args.pulse_width_us

    duration_s = float(times_s[-1]) + 0.001  # add small tail
    n_hi = int(math.ceil(duration_s * hi_sr)) + 1
    print("Events:", len(times_s), "duration_s:", duration_s, "hi_sr:", hi_sr, "n_hi:", n_hi)

    # create high-rate array
    hi = np.zeros(n_hi, dtype=np.float64)

    if pulse_w_us > 0.0:
        pulse_samples = max(1, int(round((pulse_w_us * 1e-6) * hi_sr)))
        print("Using rectangular pulses width (samples):", pulse_samples)
    else:
        pulse_samples = 0

    # Place impulses/rectangles
    idxs = np.round(times_s * hi_sr).astype(int)
    idxs[idxs < 0] = 0
    idxs[idxs >= n_hi] = n_hi-1
    for k, idx in enumerate(idxs):
        v = vals[k] * scale
        if pulse_samples <= 1:
            hi[idx] += v  # accumulate if multiple events same index
        else:
            end = min(n_hi, idx + pulse_samples)
            hi[idx:end] += v

    # Low-pass filter (RC IIR) to simulate integrator / reconstruction
    print("Applying RC low-pass: fc=", fc)
    y_hi = rc_iir_filter(hi, hi_sr, fc)

    # Decimate to out_sr (simple downsample by picking samples; pre-filtered)
    decimation = int(round(hi_sr / out_sr))
    if decimation <= 1:
        y_out = y_hi
    else:
        # pick every decimation-th sample starting from offset to align times
        y_out = y_hi[::decimation]

    # Ensure length and scaling to int16
    # normalize to avoid clipping (optional) - here scale so max maps to 80% of int16
    max_abs = np.max(np.abs(y_out)) if y_out.size>0 else 1.0
    if max_abs < 1e-9:
        max_abs = 1.0
    norm = 0.8 * 32767.0 / max_abs
    print("normalizing with factor:", norm)
    y_out_i16 = np.array([clamp_int16(v * norm) for v in y_out], dtype=np.int16)

    # write wav
    print("Writing WAV:", args.outpath, "samples:", len(y_out_i16), "out_sr:", out_sr)
    wf = wave.open(args.outpath, "wb")
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(out_sr)
    wf.writeframes(y_out_i16.tobytes())
    wf.close()
    print("Done.")

if __name__ == "__main__":
    main()

