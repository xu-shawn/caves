#!/usr/bin/env python3
"""Two more figures of docs/PROGRESSION.md (run from caves/: python3 docs/plots_looks.py) -> docs/img/looks.png.
Left: seeds a second that get a look at their tables, by step of the gate (from the gates' own logs).
Right: caverns of 40M+ per 1,000 scanned seeds by the gate's final score (stats/now/scanned_features.txt.gz: the seeds
of runs g10 - g12 that went through the scanner, with their gate numbers; hits/cv1_s3.txt: the exact caverns)."""
import gzip, collections, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
SURF, INK, INK2, MUTED, GRID, BLUE = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9', '#2a78d6'
fig, (a, b) = plt.subplots(1, 2, figsize=(12, 4.4), dpi=160, gridspec_kw={'width_ratios': [1.15, 1]}); fig.patch.set_facecolor(SURF)
for ax in (a, b):
    ax.set_facecolor(SURF); ax.set_axisbelow(True); ax.tick_params(colors=MUTED, length=0)
    for s in ('top', 'right'): ax.spines[s].set_visible(False)
    for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
steps = [('06:06  closed-form table gate, 12 CPU threads', 800), ('07:04  first look on the GPU', 5400), ('07:28  kernel in shared memory', 12700), ('10:08  cheaper GPU kernel in front', 40000),
         ('10:42  16-bit looks on CPU threads', 146000), ('10:55  thread counts inside the CPU quota', 212000), ('11:01  scan kernel in two passes', 237000)]
y = np.arange(len(steps))[::-1]; v = np.array([s[1] for s in steps])
a.hlines(y, 500, v, color=GRID, lw=2); a.plot(v, y, 'o', ms=8, color=BLUE, zorder=3)
for yi, (name, value) in zip(y, steps): a.text(value * 1.25, yi, f'{value:,}', va='center', ha='left', fontsize=9.5, color=INK2)
a.set_yticks(y); a.set_yticklabels([s[0] for s in steps], fontsize=9.5, color=INK2); a.set_xscale('log'); a.set_xlim(500, 1.5e6); a.grid(True, axis='x', color=GRID, lw=0.8)
a.set_xlabel('seeds a second that get a look at their tables', color=INK2); a.set_title('How many seeds the gate looks at', loc='left', fontsize=12, color=INK, fontweight='bold', pad=10)
best = collections.defaultdict(float)
for l in open('hits/cv1_s3.txt'):
    w = l.split()
    if len(w) > 14: best[w[0]] = max(best[w[0]], int(w[3]) / 1e6)
final, vol = [], []
for l in gzip.open('stats/now/scanned_features.txt.gz', 'rt'):
    w = l.split()
    if len(w) == 8: final.append(float(w[2])); vol.append(best.get(w[1], 0.0))
final, vol = np.array(final), np.array(vol); edges = [-9, -0.62, -0.61, -0.60, -0.59, -0.58, -0.57, 0]; names = ['under\n-0.62', '-0.62\n-0.61', '-0.61\n-0.60', '-0.60\n-0.59', '-0.59\n-0.58', '-0.58\n-0.57', 'over\n-0.57']
rate, count = [], []
for lo, hi in zip(edges[:-1], edges[1:]): m = (final > lo) & (final <= hi); rate.append(1000 * np.mean(vol[m] >= 40)); count.append(int(m.sum()))
x = np.arange(len(rate)); b.bar(x, rate, width=0.72, color=BLUE)
for xi, r, n in zip(x, rate, count): b.text(xi, r + 0.25, f'{r:.1f}' if r >= 1 else f'{r:.2f}', ha='center', va='bottom', fontsize=9.5, color=INK2)
b.set_xticks(x); b.set_xticklabels([f'{nm}\n{n:,}' for nm, n in zip(names, count)], fontsize=8.5, color=INK2); b.grid(True, axis='y', color=GRID, lw=0.8); b.set_ylim(0, max(rate) * 1.15)
b.set_xlabel('gate score (lower is better); last row: seeds scanned', color=INK2, fontsize=9.5); b.set_ylabel('caverns of 40M+ per 1,000 seeds', color=INK2)
b.set_title('Where the giants come from', loc='left', fontsize=12, color=INK, fontweight='bold', pad=10)
fig.tight_layout(w_pad=2.5); fig.savefig('docs/img/looks.png', facecolor=SURF)
print('bands:', list(zip(count, [round(r, 2) for r in rate])))
