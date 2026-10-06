#!/usr/bin/env python3
"""Figure for "what limits a cavern": left, the open cross-section of the 12 largest caverns by height, as measured and
with the aquifers' water taken out; right, the two lowest noise octaves around caverns of three size classes, ring by
ring. Data: stats/limits/ (prof_normal.txt, prof_dry.txt from the diagnostic measurer; rings.json from stats/sitefeatures).
usage: plots_limits.py  ->  docs/img/limits.png"""
import os, json, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
here = os.path.dirname(os.path.abspath(__file__)); D = os.path.join(here, '..', 'stats', 'limits')
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'; BLUE, ORANGE, GREEN = '#2a78d6', '#eb6834', '#3a9a5b'
def prof(f):
    out = []
    for l in open(os.path.join(D, f)):
        if l.startswith('profile'): out.append((int(l.split()[2]), np.array([int(x) for x in l.split(':')[1].split()], float)))
    return out
a = prof('prof_normal.txt'); b = prof('prof_dry.txt'); order = sorted(range(len(a)), key=lambda i: -a[i][1].sum())[:12]
ya = np.mean([a[i][1] for i in order], 0) / 8 / 1e3; yb = np.mean([b[i][1] for i in order], 0) / 8 / 1e3; ys = -64 + 8 * np.arange(len(ya)) + 4
fig, ax = plt.subplots(1, 2, figsize=(11, 4.6), dpi=150); fig.patch.set_facecolor(SURF)
for x in ax:
    x.set_facecolor(SURF); x.set_axisbelow(True); x.tick_params(colors=MUTED, length=0); x.grid(True, color=GRID, lw=0.8)
    for s in ('top', 'right'): x.spines[s].set_visible(False)
    for s in ('left', 'bottom'): x.spines[s].set_color(GRID)
m = ys <= 200
ax[0].plot(yb[m], ys[m], color=BLUE, lw=2); ax[0].plot(ya[m], ys[m], color=INK, lw=2)
ax[0].text(yb[m].max() * 0.99, 62, 'without the aquifers\' water', color=BLUE, fontsize=9, ha='right'); ax[0].text(ya[9] * 0.55, -22, 'as measured', color=INK, fontsize=9, ha='center')
ax[0].set_xlabel('open columns at that height, thousands (mean of the 12 largest caverns)', color=INK2); ax[0].set_ylabel('height y', color=INK2); ax[0].set_xlim(0); ax[0].set_ylim(-64, 200)
ax[0].set_title('A sheet 50 blocks thick', loc='left', fontsize=12, color=INK, fontweight='bold')
r = json.load(open(os.path.join(D, 'rings.json'))); mid = np.array([160, 480, 800, 1120, 1440, 1760])
for (name, c), dx in zip((('30-40M', MUTED), ('40-46M', ORANGE), ('46M+', INK)), (-18, 0, 18)):
    k = r['classes'][name]; v = np.array(k['octave8_low']) + np.array(k['octave7_low']); e = np.hypot(k['se8'], k['se7'])
    ax[1].errorbar(mid + dx, v, yerr=2 * e, fmt='o-', ms=4, lw=1.8, color=c, ecolor=c, elinewidth=1, capsize=2, label=f"caverns of {name}  ({k['n']:,})")
ax[1].axvspan(0, 640, color=GRID, alpha=0.5, lw=0); ax[1].text(320, 0.012, 'the 1,280-block window', color=INK2, fontsize=8.5, ha='center')
ax[1].set_xlabel('distance from the middle of the cavern, blocks (square rings)', color=INK2); ax[1].set_ylabel('octaves -8 and -7, mean over the ring (y -40..40)', color=INK2); ax[1].set_xlim(0, 1920)
ax[1].set_title('The largest are wider, not deeper', loc='left', fontsize=12, color=INK, fontweight='bold')
leg = ax[1].legend(loc='lower right', frameon=False, fontsize=9); [t.set_color(INK2) for t in leg.get_texts()]
fig.tight_layout(); fig.savefig(os.path.join(here, 'img', 'limits.png'), facecolor=SURF); print('wrote docs/img/limits.png')
