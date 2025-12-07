# compute_event_ticks.py
import csv,sys
emuclk=3579545.0
audio=44100.0
iter_per_sample = emuclk*2.0/audio
ticks=[]
with open('ym2413_scale_chromatic.delta.csv') as f:
    r = csv.reader(f)
    next(r)
    cum=0
    for row in r:
        if not row: continue
        d = int(row[0])
        cum += d
        ticks.append((cum, row[1], row[2], int(round(cum*iter_per_sample))))
print("events:", len(ticks))
print("first 10 (sample, reg, data, tick):")
for t in ticks[:10]:
    print(t)
print("last 10:")
for t in ticks[-10:]:
    print(t)
print("min_tick=", ticks[0][3], "max_tick=", ticks[-1][3])
# count how many ticks <= suggested sim_cycles
suggested = int(ticks[-1][3] + 1000)
print("suggested sim_cycles ~", suggested)

