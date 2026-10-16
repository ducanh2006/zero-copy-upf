#!/usr/bin/env python3
"""plot.py - bar chart from bench/results.csv -> bench/compare.png.
Falls back to ASCII table when matplotlib is unavailable.
Usage: python3 bench/plot.py [results.csv] [compare.png]
"""
import csv
import os
import sys

csv_path = sys.argv[1] if len(sys.argv) > 1 else "bench/results.csv"
png_path = sys.argv[2] if len(sys.argv) > 2 else "bench/compare.png"

if not os.path.exists(csv_path):
    print(f"results file not found: {csv_path}", file=sys.stderr)
    sys.exit(1)

rows = []
with open(csv_path, newline="") as f:
    for r in csv.DictReader(f):
        rows.append(r)

if not rows:
    print("empty results.csv", file=sys.stderr)
    sys.exit(1)

# ASCII fallback (always printed)
print(f"{'mode':<8} {'method':<10} {'avg_cyc':>10} {'avg_ns':>10} {'mpps':>8}")
for r in rows:
    print(f"{r['mode']:<8} {r['method']:<10} {float(r['avg_cycles']):>10.1f} "
          f"{float(r['avg_ns']):>10.1f} {float(r['mpps']):>8.3f}")

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except Exception as e:
    print(f"matplotlib unavailable ({e}), ASCII table above is the chart fallback.")
    sys.exit(0)

# Group by mode
modes = sorted(set(r["mode"] for r in rows))
n = len(modes)
fig, axes = plt.subplots(1, max(n, 1), figsize=(5 * max(n, 1), 4), squeeze=False)
for ax, mode in zip(axes[0], modes):
    dc = next((r for r in rows if r["mode"] == mode and r["method"] == "deepcopy"), None)
    zc = next((r for r in rows if r["mode"] == mode and r["method"] == "zerocopy"), None)
    vals = []
    labels = []
    if dc:
        vals.append(float(dc["avg_cycles"]))
        labels.append("deepcopy\n1700B")
    if zc:
        vals.append(float(zc["avg_cycles"]))
        labels.append("zerocopy\n100B")
    bars = ax.bar(labels, vals)
    ax.set_title(f"avg cycles / duplicate ({mode})")
    ax.set_ylabel("cycles")
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, b.get_height(), f"{v:.1f}",
                ha="center", va="bottom", fontsize=9)

plt.tight_layout()
plt.savefig(png_path, dpi=150)
print(f"wrote {png_path}")
