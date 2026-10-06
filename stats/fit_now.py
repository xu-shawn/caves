#!/usr/bin/env python3
"""The rarity curves per box-hour of every pipeline version so far, one figure (run from caves/stats).
v1-v4: exact caverns measured (tail.py's weighted counts) and their fits. v5: the latest slice of run g5, where only the
biggest formula caverns are measured exactly: the curve predicted from its formula caverns (predict.py), with the exact
counts as points where they are complete."""
import sys, os, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here); import tail, predict
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'
COLORS = ['#898781', '#2a78d6', '#eb6834', '#3a9a5b', '#9b59b6', '#c0392b']
out = sys.argv[1]; first, rate5, clock = int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]   # v5: g5 seeds from this line on, its seeds per box-hour, the time of day
v6 = (int(sys.argv[5]), float(sys.argv[6]), sys.argv[7]) if len(sys.argv) > 7 else None        # v6: the same three for the stretch with the early exit (read from ../final/cv1/runs/g5)
fig, ax = plt.subplots(figsize=(10.5, 6.6), dpi=150); fig.patch.set_facecolor(SURF); ax.set_facecolor(SURF); ax.set_axisbelow(True); ax.tick_params(colors=MUTED, length=0); ax.grid(True, color=GRID, lw=0.8)
for s in ('top', 'right'): ax.spines[s].set_visible(False)
for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
T = np.arange(22, 40.01, 1.0); lines = []
versions = [('v1 00:00  seed gate, pair trick, formula, exact', 'v1', 437, 24), ('v2 00:55  fast exact measure', 'v2', 615, 24), ('v3 02:05  terrain-first scanner, fast Perlin', 'v3', 1406, 26), ('v4 02:41  phase gate from the RNG', 'v4', 1650, 30)]
for k, (label, d, rate, lo) in enumerate(versions):
    seeds, distinct, rows = tail.load(os.path.join(here, d)); n, e = tail.tail(rows, seeds, T); m = (T >= lo) & (n > 0); a, b = tail.fit(T[m], n[m], e[m]); c = COLORS[k]
    ax.errorbar(T[m], n[m] * rate, yerr=[np.minimum(e[m], n[m] * 0.9) * rate, e[m] * rate], fmt='o', ms=4.5, color=c, ecolor=c, elinewidth=1, capsize=2, zorder=3)
    xs = np.array([lo, 41.0]); ax.plot(xs, 10 ** (a + b * xs) * rate, color=c, lw=1.8, label=label); lines.append((label, a + np.log10(rate), b))
P = os.path.join(here, 'pull3/'); pooled = predict.pool([P + f for f in ('g2_s3.txt', 'ab3_s3.txt', 'g3_s3.txt', 'g4_s3.txt', 'g5_s3.txt')])
g5 = [w for w in (l.split() for l in open(P + 'g5_seeds.txt')) if len(w) >= 9][first:(v6[0] if v6 else None)]; keep = {w[0] for w in g5}
form = predict.distinct(P + 'g5_s2.txt', keep); Tp = np.arange(28, 39.01, 1.0); pred = predict.curve(form, len(keep), pooled, Tp); ok = pred > 0
b5, a5 = np.polyfit(Tp[ok][:9], np.log10(pred[ok][:9] * rate5), 1); c = COLORS[4]
ax.plot(Tp[ok], pred[ok] * rate5, color=c, lw=2.4, label=f'v5 {clock}  analytic table gate, terrain phases, scanner x4 (from its formula caverns)')
seen = set(); ex = []
for l in open(P + 'g5_s3.txt'):
    w = l.split()
    if w[0] in keep and (w[0], w[3]) not in seen: seen.add((w[0], w[3])); ex.append(float(w[3]) / 1e6)
ex = np.array(ex); Te = np.arange(34, 37.01, 1.0); ne = np.array([np.sum(ex >= t) for t in Te]); me = ne > 0
ax.errorbar(Te[me], ne[me] / len(keep) * rate5, yerr=np.sqrt(ne[me]) / len(keep) * rate5 * 0.999, fmt='D', ms=5, color=c, ecolor=c, elinewidth=1, capsize=2, zorder=4)
lines.append((f'v5 {clock}', a5, b5))
if v6:
    Q = os.path.join(here, '../final/cv1/runs/'); pooled = predict.pool([Q + f for f in ('g2/s3.txt', 'g3/s3.txt', 'g4/s3.txt', 'g5/s3.txt')] + [P + 'ab3_s3.txt'])
    rows6 = [w for w in (l.split() for l in open(Q + 'g5/seeds.txt')) if len(w) >= 9][v6[0]:]; keep = {w[0] for w in rows6}
    form = predict.distinct(Q + 'g5/s2_19M.txt', keep); Tp = np.arange(30, 41.01, 1.0); pred = predict.curve(form, len(keep), pooled, Tp, floor=21.0); ok = pred > 0
    b6, a6 = np.polyfit(Tp[ok][:9], np.log10(pred[ok][:9] * v6[1]), 1); c = COLORS[5]
    ax.plot(Tp[ok], pred[ok] * v6[1], color=c, lw=2.4, label=f'v6 {v6[2]}  scanner stops early on the terrain cut, base tables first (from its formula caverns)')
    seen = set(); ex = []
    for l in open(Q + 'g5/s3.txt'):
        w = l.split()
        if w[0] in keep and (w[0], w[3]) not in seen: seen.add((w[0], w[3])); ex.append(float(w[3]) / 1e6)
    ex = np.array(ex); Te = np.arange(34, 40.01, 1.0); ne = np.array([np.sum(ex >= t) for t in Te]); me = ne > 0
    ax.errorbar(Te[me], ne[me] / len(keep) * v6[1], yerr=np.sqrt(ne[me]) / len(keep) * v6[1] * 0.999, fmt='D', ms=5, color=c, ecolor=c, elinewidth=1, capsize=2, zorder=4)
    lines.append((f'v6 {v6[2]}', a6, b6)); print(f'v6: {len(keep)} seeds, {len(form)} formula caverns of 21M+, exact >= 34/36/38/40M: ' + ' '.join(str(int(np.sum(ex >= t))) for t in (34, 36, 38, 40)))
ax.set_yscale('log'); ax.set_xlim(23.5, 41.5); ax.set_ylim(5e-3, 3e3); ax.set_xlabel('cavern volume V, million blocks (exact, 7-block rule)', color=INK2); ax.set_ylabel('distinct caverns of at least V found per box-hour ($0.155)', color=INK2)
ax.set_title('What one box finds per hour, by pipeline version', loc='left', fontsize=13, color=INK, fontweight='bold')
leg = ax.legend(loc='lower left', frameon=False, fontsize=9); [t.set_color(INK2) for t in leg.get_texts()]
fig.tight_layout(); fig.savefig(out, facecolor=SURF)
for label, a, b in lines: print(f'{label[:60]:60s} log10(per hour) = {a:6.2f} {b:+.3f} V (decade per {-1 / b:.2f}M): ' + ', '.join(f'>={t}M: {10 ** (a + b * t):.3g}/h' for t in (30, 33, 36, 38)))
