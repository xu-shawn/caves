#!/usr/bin/env python3
"""Pipeline versions side by side: the rarity curves of exact caverns per processed seed and per box-hour.
usage: compare.py OUT.png 'LABEL|DIR|SEEDS_PER_HOUR[|FROM]' ...   (DIR as for tail.py; SEEDS_PER_HOUR = seeds the box got
through per hour in that version, gate included; FROM = the volume from which that version's exact list is complete, the
fit and the points start there)"""
import sys, os, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here); import tail
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'
COLORS = ['#898781', '#2a78d6', '#eb6834', '#3a9a5b', '#9b59b6']
out = sys.argv[1]; versions = []
for a in sys.argv[2:]:
    w = a.split('|'); versions.append(dict(label=w[0], dir=w[1], rate=float(w[2]), lo=float(w[3]) if len(w) > 3 else 22.0))
fig, axes = plt.subplots(1, 2, figsize=(13.5, 5.6), dpi=150); fig.patch.set_facecolor(SURF)
for ax in axes:
    ax.set_facecolor(SURF); ax.set_axisbelow(True); ax.tick_params(colors=MUTED, length=0); ax.grid(True, color=GRID, lw=0.8)
    for s in ('top', 'right'): ax.spines[s].set_visible(False)
    for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
T = np.arange(18, 36.01, 1.0); lines = []
for k, v in enumerate(versions):
    seeds, distinct, rows = tail.load(v['dir']); n, e = tail.tail(rows, seeds, T); c = COLORS[k % len(COLORS)]
    m = (T >= v['lo']) & (n > 0); a, b = tail.fit(T[m], n[m], e[m]); v.update(a=a, b=b, seeds=seeds, measured=len(rows), best=max(r[0] for r in rows))
    for ax, scale, unit in ((axes[0], 1.0, 'seed'), (axes[1], v['rate'], 'box-hour')):
        ax.errorbar(T[m], n[m] * scale, yerr=[np.minimum(e[m], n[m] * 0.9) * scale, e[m] * scale], fmt='o', ms=5, color=c, ecolor=c, elinewidth=1, capsize=2, zorder=3)
        xs = np.array([v['lo'], 37.0]); ax.plot(xs, 10 ** (a + b * xs) * scale, color=c, lw=2, label=f"{v['label']}: 10x rarer per {-1 / b:.2f}M")
    lines.append(f"{v['label']:38s} {seeds:4d} seeds, {len(rows):5d} exact; log10 N/seed = {a:6.3f} {b:+.4f} V (decade per {-1 / b:.2f}M); {v['rate']:5.0f} seeds/box-hour; "
                 + ', '.join(f">={t}M: {10 ** (a + b * t) * v['rate']:.3g}/h" for t in (26, 30, 33, 36)))
for ax, unit in ((axes[0], 'processed seed'), (axes[1], 'box-hour ($0.155)')):
    ax.set_yscale('log'); ax.set_xlim(21.5, 37.5); ax.set_xlabel('cavern volume V, million blocks (exact, 7-block rule)', color=INK2); ax.set_ylabel(f'distinct caverns of at least V, per {unit}', color=INK2)
    leg = ax.legend(loc='lower left', frameon=False, fontsize=9.5); [t.set_color(INK2) for t in leg.get_texts()]
axes[0].set_title('Per processed seed', loc='left', fontsize=12.5, color=INK, fontweight='bold'); axes[1].set_title('Per box-hour: what the search finds', loc='left', fontsize=12.5, color=INK, fontweight='bold')
fig.tight_layout(w_pad=2.5); fig.savefig(out, facecolor=SURF); print('\n'.join(lines))
