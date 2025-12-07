#!/usr/bin/env python3
# list_missed_events.py
# Lists events that do NOT fall inside non-zero clusters (based on nonzero_clusters.csv).
import csv
# read clusters
clusters = []
with open("nonzero_clusters.csv") as f:
    r = csv.reader(f)
    next(r)
    for row in r:
        start = int(row[0])
        length = int(row[1])
        clusters.append((start, start+length-1))
# read events and mapped sample_idx (reuse compute_event_ticks mapping)
events = []
with open("ym2413_scale_chromatic.delta.csv") as f:
    next(f)
    cum = 0
    for i,row in enumerate(csv.reader(f), start=1):
        if not row or row[0]=="":
            continue
        d = int(row[0]); reg = row[2] if len(row)>2 else ""; data=row[3] if len(row)>3 else ""
        cum += d
        events.append((i, cum, reg, data))
# compute iter_per_sample
emuclk = 3579545.0; audio = 44100.0
iter_per_sample = emuclk*2.0/audio
missed = []
for idx, cum, reg, data in events:
    tick = int(round(cum * iter_per_sample))
    sample_idx = int(round(cum))  # cum is already sample count (we'll compare sample index)
    # check if sample_idx falls in any cluster
    hit = False
    for a,b in clusters:
        if sample_idx >= a and sample_idx <= b:
            hit = True; break
    if not hit:
        missed.append((idx, sample_idx, reg, data))
# output
print("total events:", len(events))
print("events hitting clusters:", len(events)-len(missed))
print("missed count:", len(missed))
print("first 40 missed events (event_idx, sample_idx, reg, data):")
for e in missed[:40]:
    print(e)

