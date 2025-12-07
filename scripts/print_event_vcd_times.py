#!/usr/bin/env python3
# print_event_vcd_times.py
# Usage:
#   python3 print_event_vcd_times.py [csv_path] [emuclk_hz] [audio_rate] [vcd_ps_per_tick]
# Defaults:
#   csv_path = event_amplitude_report.csv
#   emuclk_hz = 3579545.0
#   audio_rate = 44100
#   vcd_ps_per_tick = 139683

import csv, sys, math

csv_path = sys.argv[1] if len(sys.argv) > 1 else "event_amplitude_report.csv"
emuclk_hz = float(sys.argv[2]) if len(sys.argv) > 2 else 3579545.0
audio_rate = float(sys.argv[3]) if len(sys.argv) > 3 else 44100.0
vcd_ps_per_tick = float(sys.argv[4]) if len(sys.argv) > 4 else 139683.0

iter_per_sample = (emuclk_hz * 2.0) / audio_rate  # tb ticks per audio sample
ps_per_tick = 1e12 / (2.0 * emuclk_hz)            # picoseconds per tb tick (informational)

print("# Using emuclk_hz={}, audio_rate={}, iter_per_sample={:.6f}, vcd_ps_per_tick={}".format(
    emuclk_hz, int(audio_rate), iter_per_sample, int(vcd_ps_per_tick)
))
print("idx,sample_cum,reg,data,max_abs_post,tick,time_us,vcd_time_ps")
with open(csv_path, newline='') as f:
    r = csv.reader(f)
    hdr = next(r)
    for row in r:
        if not row:
            continue
        idx = int(row[0])
        sample_cum = int(row[1])
        reg = row[2]
        data = row[3]
        # max_abs_post is column 7 in event_amplitude_report.csv (0-based index 7)
        try:
            max_post = float(row[7])
        except:
            max_post = 0.0
        tick = int(round(sample_cum * iter_per_sample))
        time_us = tick / (2.0 * emuclk_hz) * 1e6
        vcd_time_ps = int(round(tick * vcd_ps_per_tick))
        print("{},{},{},{},{:.0f},{},{:.6f},{}".format(
            idx, sample_cum, reg, data, max_post, tick, time_us, vcd_time_ps
        ))

