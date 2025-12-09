#!/usr/bin/env python3
"""
Convert a newline-separated decimal samples text file into a 16-bit PCM WAV file.

Usage:
  python3 tools/samples_txt_to_wav.py samples.txt out.wav --sr 44100
"""
import sys
import wave
import struct
import argparse

p = argparse.ArgumentParser(description="Convert samples text -> 16-bit WAV")
p.add_argument('infile')
p.add_argument('outfile')
p.add_argument('--sr', type=int, default=44100, help='sample rate (default 44100)')
args = p.parse_args()

vals = []
with open(args.infile, 'r') as f:
    for ln in f:
        ln = ln.strip()
        if not ln: continue
        try:
            v = int(ln)
        except:
            continue
        vals.append(v)

if not vals:
    print("No samples found in", args.infile); sys.exit(2)

# detect max abs to scale to int16 if needed
maxabs = max(abs(v) for v in vals)
if maxabs == 0:
    maxabs = 1

# if values already in int16 range, keep; otherwise scale
scale = 1.0
if maxabs <= 32767:
    scale = 1.0
else:
    scale = 32767.0 / maxabs

with wave.open(args.outfile, 'w') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(args.sr)
    for v in vals:
        iv = int(round(v * scale))
        if iv > 32767: iv = 32767
        if iv < -32768: iv = -32768
        wf.writeframes(struct.pack('<h', iv))

print("WAV written:", args.outfile, "samples=", len(vals), "sr=", args.sr)

