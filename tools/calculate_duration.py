#!/usr/bin/env python3
import csv

INPUT  = "acc_log.csv"
OUTPUT = "acc_segments.csv"

def main():
    rows = []
    with open(INPUT, newline="") as f:
        r = csv.reader(f)
        header = next(r, None)
        for t_ps_str, acc_str in r:
            t_ps = int(t_ps_str)
            acc  = int(acc_str)
            rows.append((t_ps, acc))

    if not rows:
        print("no rows")
        return

    segments = []
    cur_acc = rows[0][1]
    seg_start_t = rows[0][0]

    for (t_ps, acc) in rows[1:]:
        if acc != cur_acc:
            # ここで一つ前の値の区間が確定
            seg_end_t = t_ps  # 変化直前まで続いているとみなす
            segments.append((seg_start_t, seg_end_t, cur_acc))

            # 新しい区間開始
            cur_acc = acc
            seg_start_t = t_ps

    # 最後の区間を閉じる
    last_t = rows[-1][0]
    segments.append((seg_start_t, last_t, cur_acc))

    # 書き出し（秒に直して duration も付ける）
    with open(OUTPUT, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_start_s", "t_end_s", "duration_s", "acc"])
        for t0_ps, t1_ps, acc in segments:
            t0_s = t0_ps * 1e-12
            t1_s = t1_ps * 1e-12
            dur  = t1_s - t0_s
            w.writerow([f"{t0_s:.12f}", f"{t1_s:.12f}", f"{dur:.12f}", acc])

    print(f"wrote {len(segments)} segments to {OUTPUT}")

if __name__ == "__main__":
    main()

