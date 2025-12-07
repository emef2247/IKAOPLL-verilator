#!/usr/bin/env python3
# name=analyze_pcm.py
import struct
import os
fn = "out.pcm"
size = os.path.getsize(fn)
n = size // 2
zeros = 0
mn = 2**15
mx = -2**15
with open(fn,"rb") as f:
    for i in range(n):
        b = f.read(2)
        if len(b)<2: break
        v = struct.unpack("<h", b)[0]
        if v == 0: zeros += 1
        if v < mn: mn = v
        if v > mx: mx = v
print("samples:", n)
print("zeros:", zeros, "({:.2f}%)".format(zeros/n*100 if n else 0))
print("min:", mn, "max:", mx)

