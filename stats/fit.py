#!/usr/bin/env python3
"""The rarity fits of the cave search, as one figure.     usage: fit.py DIR OUT.png [LABEL]     (DIR as for tail.py)
  A  distinct exact caverns of at least V blocks per processed seed (weighted sample, see tail.py) and the exponential fit
  B  the seed gate: the share of seeds at or below a gate value, and its fit (gate_hist.txt: gate bin, count)
  C  what the gate buys: a seed's biggest formula cavern against its gate value
  D  the formula cavern as a predictor of the exact one"""
import sys, os, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt, matplotlib.ticker
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here); import tail
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'
BLUE, ORANGE, GREEN, PURPLE = '#2a78d6', '#eb6834', '#3a9a5b', '#9b59b6'
d, out = sys.argv[1], sys.argv[2]; label = sys.argv[3] if len(sys.argv) > 3 else os.path.basename(d.rstrip('/'))
seeds, distinct, rows = tail.load(d)
fig, axes = plt.subplots(2, 2, figsize=(13.5, 9.6), dpi=150); fig.patch.set_facecolor(SURF)
for ax in axes.flat:
    ax.set_facecolor(SURF); ax.set_axisbelow(True); ax.tick_params(colors=MUTED, length=0); ax.grid(True, color=GRID, lw=0.8)
    for s in ('top', 'right'): ax.spines[s].set_visible(False)
    for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
def title(ax, text, sub): ax.set_title(text + '\n', loc='left', fontsize=12.5, color=INK, fontweight='bold', pad=4); ax.text(0, 1.025, sub, transform=ax.transAxes, fontsize=9.5, color=INK2)

# A: the rarity curve
ax = axes[0, 0]; T = np.arange(14, 34.01, 1.0); n, e = tail.tail(rows, seeds, T)
form = np.array([f for r in distinct.values() for f, x, y, z in r]) / 1e6; nf = np.array([np.sum(form >= t) / seeds for t in T])
lo, hi = 22.0, 31.0; m = (T >= lo) & (T <= hi) & (n > 0); a, b = tail.fit(T[m], n[m], e[m]); mf = (T >= 16) & (nf > 0); af, bf = np.polyfit(T[mf], np.log10(nf[mf]), 1)[::-1]
ok = n > 0
ax.errorbar(T[ok], n[ok], yerr=[np.minimum(e[ok], n[ok] * 0.9), e[ok]], fmt='o', ms=6, color=BLUE, ecolor=BLUE, elinewidth=1.2, capsize=2.5, label='exact caverns (measured sample, weighted)', zorder=3)
xs = np.array([lo - 3, 36]); ax.plot(xs, 10 ** (a + b * xs), color=BLUE, lw=2, label=f'fit {lo:.0f}-{hi:.0f}M: one decade per {-1 / b:.2f}M blocks')
ax.plot(T[nf > 0], nf[nf > 0], 's', ms=5, color=ORANGE, mfc='none', mew=1.5, label='formula caverns (all of them, the predictor)')
ax.plot(xs - 3, 10 ** (af + bf * (xs - 3)), color=ORANGE, lw=1.3, ls=(0, (4, 3)), label=f'fit: one decade per {-1 / bf:.2f}M')
ax.set_yscale('log'); ax.set_xlim(13.5, 38.5); ax.set_ylim(1e-4, 80); ax.set_xlabel('cavern volume V, million blocks', color=INK2); ax.set_ylabel('distinct caverns of at least V, per processed seed', color=INK2)
for v in (30, 33):
    y = 10 ** (a + b * v); ax.annotate(f'{v}M: 1 in {1 / y:,.0f} seeds', (v, y), xytext=(v + 0.8, y * 3), fontsize=9, color=INK2, arrowprops=dict(arrowstyle='-', color=MUTED, lw=0.8), ha='left')
leg = ax.legend(loc='upper right', frameon=False, fontsize=9); [t.set_color(INK2) for t in leg.get_texts()]
title(ax, 'A. Rarity of big caverns', f'{label}: {seeds} processed seeds, {len(rows)} exact measurements; log10 N = {a:.2f} {b:+.3f} V')

# B: the gate
ax = axes[0, 1]; h = np.loadtxt(os.path.join(here, 'gate_hist.txt')); g = h[:, 0]; c = np.cumsum(h[:, 1]); total = c[-1]; share = c / total
mb = (share >= 3e-5) & (share <= 2e-2); bb, ab = np.polyfit(g[mb], np.log10(share[mb]), 1)
ax.plot(g, share, 'o', ms=3.5, color=PURPLE, label=f'{total:,.0f} seeds through the gate'); xs = np.array([-0.325, -0.285]); ax.plot(xs, 10 ** (ab + bb * xs), color=PURPLE, lw=1.6, label=f'fit: 10x rarer per {1 / bb:.4f} lower')
ax.set_yscale('log'); ax.set_xlim(-0.325, -0.20); ax.set_ylim(3e-6, 1.5); ax.set_xlabel('seed gate value (lower = lower cheese noise somewhere in the world)', color=INK2); ax.set_ylabel('share of seeds at or below', color=INK2)
ax.axvline(-0.300, color=MUTED, lw=1, ls=(0, (2, 2))); ax.text(-0.2995, 2e-5, f'pass line -0.300\n= {10 ** (ab + bb * -0.300) * 100:.2f}% of seeds', fontsize=9, color=INK2)
leg = ax.legend(loc='upper left', frameon=False, fontsize=9); [t.set_color(INK2) for t in leg.get_texts()]
title(ax, 'B. The seed gate is exponential too', f'log10 share = {ab:.2f} + {bb:.1f} gate   (0.48 CPU-s per seed)')

# C: gate against the seed's biggest formula cavern
ax = axes[1, 0]; S = np.array([[float(w[1]), float(w[2]) / 1e6] for w in (l.split() for l in open(d + '/seeds.txt')) if len(w) >= 3 and float(w[2]) > 0])
ax.plot(S[:, 0], S[:, 1], 'o', ms=4, color=GREEN, alpha=0.75, mew=0, label=f'{len(S)} processed seeds')
slope, icpt = np.polyfit(S[:, 0], S[:, 1], 1); res = S[:, 1] - (icpt + slope * S[:, 0]); xs = np.array([S[:, 0].min(), S[:, 0].max()])
ax.plot(xs, icpt + slope * xs, color=INK, lw=1.4, label=f'{slope / 100:+.2f}M per 0.01 of gate; scatter {res.std():.2f}M')
ax.set_xlabel('seed gate value', color=INK2); ax.set_ylabel("seed's biggest formula cavern, million blocks", color=INK2); ax.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(6))
leg = ax.legend(loc='lower left', frameon=False, fontsize=9); [t.set_color(INK2) for t in leg.get_texts()]
per_decade = -slope / bb; title(ax, 'C. What a lower gate buys', f'10x rarer seeds: +{per_decade:.2f}M on the best formula cavern = x{10 ** (per_decade * -bf):.2f} big caverns per seed')

# D: predictor against the truth
ax = axes[1, 1]; V = np.array([r[0] for r in rows]); F = np.array([r[1] for r in rows]); full = np.array([r[3] >= 20 for r in rows])
ax.plot(F[~full], V[~full], 'o', ms=4, color=BLUE, alpha=0.7, mew=0, label='random sample of formula caverns of 12-20M'); ax.plot(F[full], V[full], 'o', ms=4, color=ORANGE, alpha=0.8, mew=0, label='every formula cavern of 20M+')
xs = np.array([11.5, 26]); ax.plot(xs, xs, color=INK, lw=1.2); ax.plot(xs, 1.2 * xs, color=MUTED, lw=1, ls=(0, (4, 3))); ax.plot(xs, 1.35 * xs, color=MUTED, lw=1, ls=(0, (1, 2)))
ax.text(25.9, 25.9 - 0.9, 'exact = formula', fontsize=8.5, color=INK2, ha='right', va='top'); ax.text(25.9, 25.9 * 1.2 - 0.8, 'x 1.2', fontsize=8.5, color=INK2, ha='right', va='top'); ax.text(23.6, 23.6 * 1.35 + 0.3, 'x 1.35', fontsize=8.5, color=INK2, ha='right', va='bottom')
ratio = V[F >= 16] / F[F >= 16]
ax.set_xlim(11.5, 26); ax.set_ylim(0, 34); ax.set_xlabel('formula cavern (largest connected cluster on the 16-block grid), million blocks', color=INK2); ax.set_ylabel('exact cavern, million blocks', color=INK2)
leg = ax.legend(loc='lower right', frameon=False, fontsize=9); [t.set_color(INK2) for t in leg.get_texts()]
title(ax, 'D. Formula cavern against the exact one', f'exact / formula over 16M+: median {np.median(ratio):.2f}, 90% below {np.percentile(ratio, 90):.2f}, 98% below {np.percentile(ratio, 98):.2f}')
fig.tight_layout(h_pad=2.2, w_pad=2.0); fig.savefig(out, facecolor=SURF)
print(f'A exact: log10 N = {a:.3f} {b:+.4f} V (decade per {-1 / b:.2f}M); formula: {af:.3f} {bf:+.4f} F (decade per {-1 / bf:.2f}M)')
print(f'B gate: log10 share = {ab:.2f} + {bb:.2f} g (decade per {1 / bb:.5f}); share at -0.300 = {10 ** (ab + bb * -0.3):.5f}')
print(f'C best formula = {icpt:.2f} {slope:+.1f} gate (M), residual sd {res.std():.2f}M; per decade of gate +{per_decade:.2f}M')
for v in (26, 28, 30, 33, 35): print(f'   exact >= {v}M: {10 ** (a + b * v):.5f} per seed (1 in {1 / 10 ** (a + b * v):,.0f})')
