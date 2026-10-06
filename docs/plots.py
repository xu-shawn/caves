#!/usr/bin/env python3
"""Figures of docs/PROGRESSION.md (run from caves/: python3 docs/plots.py). Data: stats/ (gate values and phases of 620,000
seeds, the phase calibration, the seeds with both early-exit statistics and their outcomes)."""
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'
BLUE, ORANGE, GREEN, PURPLE = '#2a78d6', '#eb6834', '#3a9a5b', '#9b59b6'
def frame(w, h, n=1):
    fig, axes = plt.subplots(1, n, figsize=(w, h), dpi=160); fig.patch.set_facecolor(SURF)
    for ax in np.atleast_1d(axes):
        ax.set_facecolor(SURF); ax.set_axisbelow(True); ax.tick_params(colors=MUTED, length=0); ax.grid(True, color=GRID, lw=0.8)
        for s in ('top', 'right'): ax.spines[s].set_visible(False)
        for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
    return fig, axes
def title(ax, text): ax.set_title(text, loc='left', fontsize=12, color=INK, fontweight='bold', pad=10)

# 1. the phase gate: the old table gate against where y = 24 sits in the cells of the lowest cheese octaves (620,000 seeds),
#    and a seed's big-cavern sites against the same (630 seeds scanned in full)
G = np.loadtxt('stats/gate_all.txt'); P = np.loadtxt('stats/phases_all.txt'); g = dict(zip(G[:, 0].astype(np.int64), G[:, 1]))
keep = np.array([int(s) in g for s in P[:, 0]]); P = P[keep]; gate = np.array([g[int(s)] for s in P[:, 0]])
F = [2.0 ** -8, 2.0 ** -8 * 1.0181268882175227, 2.0 ** -7, 2.0 ** -7 * 1.0181268882175227]
phase = np.array([(P[:, 1 + i] + 16 * F[i]) % 1.0 for i in range(4)]).T
fig, (a, b) = frame(11.5, 4.3, 2); bins = np.linspace(0, 1, 41); mid = 0.5 * (bins[1:] + bins[:-1])
for i, (lab, col) in enumerate((('octave -8 (cells 384 blocks tall)', BLUE), ('octave -7 (cells 192 blocks tall)', ORANGE))):
    for j, ls in ((2 * i, '-'), (2 * i + 1, (0, (3, 2)))):
        idx = np.minimum((phase[:, j] * 40).astype(int), 39); a.plot(mid, [gate[idx == k].mean() for k in range(40)], color=col, lw=2, ls=ls, label=lab + (', stack A' if j % 2 == 0 else ', stack B'))
a.set_xlabel('where y = 24 sits in the layer\'s cell (0 and 1 = lattice planes)', color=INK2); a.set_ylabel('mean gate value (lower = better seed)', color=INK2); a.invert_yaxis()
leg = a.legend(frameon=False, fontsize=8.5, loc='lower center'); [t.set_color(INK2) for t in leg.get_texts()]; title(a, 'The table gate is set by four RNG numbers')
a.text(0.02, 0.93, '620,000 seeds; the four offsets explain\n97% of the gate\'s variance', transform=a.transAxes, fontsize=8.5, color=INK2, va='top')
rows = {}
for f in ('stats/phasecal/local.txt', 'stats/pull3/phasecal_box.txt'):
    for l in open(f):
        w = l.replace('|', ' ').split()
        if len(w) >= 8: rows[w[0]] = float(w[1])
D = {l.split()[0]: [float(v) for v in l.split()[1:]] for l in open('stats/phasecal/design.txt')}
seeds = [s for s in rows if s in D]; y = np.array([rows[s] for s in seeds]); ph = np.array([D[s] for s in seeds])
for cols, col, lab, lo, hi, n in (((0, 1), BLUE, 'octave -8 (A and B pooled)', 0.40, 0.82, 8), ((2, 3), ORANGE, 'octave -7 (A and B pooled)', 0.0, 1.0, 10)):
    e = np.linspace(lo, hi, n + 1); x = np.concatenate([ph[:, c] for c in cols]); yy = np.concatenate([y, y]); idx = np.minimum(((x - lo) / (hi - lo) * n).astype(int), n - 1)
    b.plot(0.5 * (e[1:] + e[:-1]), [np.exp(np.mean(np.log1p(yy[idx == k]))) - 1 for k in range(n)], 'o-', color=col, lw=2, ms=5, label=lab)
b.set_yscale('log'); b.set_xlabel('where y = 24 sits in the layer\'s cell', color=INK2); b.set_ylabel('big-cavern sites per seed (geometric mean)', color=INK2)
leg = b.legend(frameon=False, fontsize=8.5); [t.set_color(INK2) for t in leg.get_texts()]; title(b, '... and so is what a seed holds'); b.axvline(0.55, color=MUTED, lw=1, ls=(0, (2, 2)))
b.text(0.56, 0.06, 'best: 0.55 / 0.60', transform=b.get_xaxis_transform(), fontsize=8.5, color=INK2)
fig.tight_layout(w_pad=2); fig.savefig('docs/img/phase.png', facecolor=SURF)

# 2. the early exit: formula caverns of 25M+ per seed against the scanner's cut (618 seeds scanned in full)
S = 'stats/earlyexit_'
A = np.loadtxt(S + 'cuts.txt')    # cut (every 16th), cut (best sixteenth), n23, n25, n27
cut, n25, n27 = A[:, 1], A[:, 3], A[:, 4]; e = np.array([255, 275, 281, 285, 289, 293, 297, 301, 306, 340]); fig, ax = frame(7.2, 4.2)
x = []; r25 = []; r27 = []; share = []
for lo, hi in zip(e[:-1], e[1:]):
    m = (cut >= lo) & (cut < hi)
    if m.sum(): x.append(f'{lo}-{hi - 1}' if hi < 340 else f'{lo}+'); r25.append(n25[m].mean()); r27.append(n27[m].mean()); share.append(m.mean())
xs = np.arange(len(x)); ax.bar(xs - 0.2, r25, width=0.38, color=BLUE, label='formula caverns of 25M+ per seed'); ax.bar(xs + 0.2, r27, width=0.38, color=ORANGE, label='27M+')
for k, s_ in enumerate(share): ax.text(xs[k], max(r25[k], r27[k]) + 0.04, f'{s_ * 100:.0f}%', ha='center', fontsize=8, color=INK2)
ax.set_xticks(xs); ax.set_xticklabels(x, fontsize=8.5, color=INK2); ax.set_xlabel('the scanner\'s cut, known a third of the way through (number above a bar: share of the seeds)', color=INK2); ax.set_ylabel('per seed', color=INK2)
leg = ax.legend(frameon=False, fontsize=9, loc='upper left'); [t.set_color(INK2) for t in leg.get_texts()]; title(ax, 'One mid-scan number says what a seed is worth')
fig.tight_layout(); fig.savefig('docs/img/cut.png', facecolor=SURF)
print('wrote docs/img/phase.png, docs/img/cut.png')

# 3. tail shaping: which sites get the exact measure. The largest formula cluster F and all the open volume O of the
#    1,536-block window, for every cavern measured exactly under the rule on both (run g5's backfill and the runs after)
rows = []; seen = set()
for path in ('hits/backfill_s3.txt', 'hits/cv1_s3.txt'):
    for l in open(path):
        w = l.split()
        if len(w) < 16 or 'formula' not in w or 'open' not in w or float(w[3]) <= 0 or (w[0], w[3]) in seen: continue
        seen.add((w[0], w[3])); rows.append((float(w[3]) / 1e6, float(w[w.index('formula') + 1]) / 1e6, float(w[w.index('open') + 1]) / 1e6))
A = np.array(rows); V, F, O = A.T
fig, a = frame(7.6, 5.4); small = V < 34; mid = (V >= 34) & (V < 38); big = V >= 38
a.scatter(F[small], O[small], s=3, color=MUTED, alpha=0.35, lw=0, label=f'under 34M ({small.sum():,})', rasterized=True)
a.scatter(F[mid], O[mid], s=20, color=ORANGE, edgecolor=SURF, lw=0.6, label=f'34M to 38M ({mid.sum()})', zorder=3)
a.scatter(F[big], O[big], s=46, color='#a3201d', edgecolor=SURF, lw=0.8, label=f'38M and more ({big.sum()})', zorder=4)
a.scatter([28.60], [44.09], s=150, marker='*', color=INK, zorder=5, label='the 49.12M record')
a.axvline(31.0, color=BLUE, lw=1.6, ls=(0, (4, 3))); a.text(31.15, 31.2, 'old rule: F at least\n70% of the record', color=BLUE, fontsize=8.5, va='bottom')
xs = np.linspace(22, 40, 50); a.plot(xs, (102 - xs) / 2, color=GREEN, lw=1.6); a.text(33.2, 34.75, 'rule in use:\nF + 2 O at least 102M', color=GREEN, fontsize=8.5, va='bottom')
a.set_xlim(22.5, 39); a.set_ylim(31, 47); a.set_xlabel('largest formula cluster F in the window, million blocks', color=INK2); a.set_ylabel('all open volume O in the window, million blocks', color=INK2)
leg = a.legend(frameon=False, fontsize=8.5, loc='upper left', title='exact volume'); leg.get_title().set_color(INK2); leg.get_title().set_fontsize(8.5); [t.set_color(INK2) for t in leg.get_texts()]
title(a, 'The giants are not where the formula cluster is biggest'); fig.tight_layout(); fig.savefig('docs/img/exact_rule.png', facecolor=SURF); plt.close(fig)

# 4. the wide gate: among 27,600 seeds that had passed the 512-block gate (run g5), the share of the exact caverns of
#    30M+ in the best part of the seeds, sorted by the 1024-block number and by the 512-block gate they were picked with;
#    and the yield by fifth of the score in run g7 (both window sizes)
feat = {l.split()[0]: float(l.split()[9]) for l in open('stats/gatefeatures_g5.txt')}
gate5 = {}
for l in open('final/cv1/runs/g5/gate.txt'):
    w = l.split()
    if len(w) >= 9 and w[0] in feat: gate5[w[0]] = float(w[1]) + float(w[8]) / 92
ex = {}
for f in ('hits/cv1_s3.txt', 'hits/local_probe_s3.txt', 'hits/backfill_s3.txt'):
    for l in open(f):
        w = l.split()
        if len(w) > 14 and float(w[3]) > 0: ex.setdefault(w[0], set()).add(int(w[3]))
seeds = [s for s in gate5]; y30 = np.array([sum(1 for v in ex.get(s, ()) if v >= 30e6) for s in seeds]); y34 = np.array([sum(1 for v in ex.get(s, ()) if v >= 34e6) for s in seeds])
fig, (a, b) = frame(11.5, 4.3, 2); q = np.linspace(0, 1, 201)
for key, col, lab in ((np.array([feat[s] for s in seeds]), ORANGE, 'sorted by the 1024-block window number'), (np.array([gate5[s] for s in seeds]), BLUE, 'sorted by the 512-block gate (in use then)')):
    order = np.argsort(key); c = np.concatenate([[0], np.cumsum(y30[order])]) / y30.sum(); a.plot(q * 100, np.interp(q * len(seeds), np.arange(len(seeds) + 1), c) * 100, color=col, lw=2.2, label=lab)
a.plot([0, 100], [0, 100], color=MUTED, lw=1, ls=(0, (2, 2))); a.set_xlim(0, 60); a.set_ylim(0, 100); a.set_xlabel('best part of the seeds, %', color=INK2); a.set_ylabel('share of the exact caverns of 30M+, %', color=INK2)
leg = a.legend(frameon=False, fontsize=8.5, loc='lower right'); [t.set_color(INK2) for t in leg.get_texts()]; title(a, 'Wider windows find the seeds that matter')
a.text(0.03, 0.93, f'{len(seeds):,} seeds past the 512-block gate, {int(y30.sum())} caverns of 30M+', transform=a.transAxes, fontsize=8.5, color=INK2)
rows = [w for w in (l.split() for l in open('stats/now/g7/seeds.txt')) if len(w) >= 9]; sc = np.array([float(w[1]) for w in rows]); secs = np.array([float(w[4]) for w in rows])
n30 = np.array([sum(1 for v in ex.get(w[0], ()) if v >= 30e6) for w in rows]); edges = np.quantile(sc, np.linspace(0, 1, 6)); per = []
for i in range(5): m = (sc >= edges[i]) & (sc <= edges[i + 1]); per.append(1000 * n30[m].sum() / m.sum())
b.bar(range(5), per, width=0.62, color=GREEN); b.set_xticks(range(5)); b.set_xticklabels(['best fifth\n(top 0.1%)', '2nd', '3rd', '4th', 'worst fifth\n(top 0.4-0.5%)'], fontsize=8.5, color=INK2)
for i, v in enumerate(per): b.text(i, v + 1.2, f'{v:.0f}', ha='center', fontsize=9, color=INK2)
b.set_ylabel('exact caverns of 30M+ per 1,000 seeds', color=INK2); b.grid(axis='x', visible=False); b.set_ylim(0, max(per) * 1.25); title(b, '... and the deeper the gate, the richer the seed')
b.text(0.97, 0.93, f'run g7: {len(rows):,} seeds, the best 0.5% by the score on both window sizes', transform=b.transAxes, fontsize=8.5, color=INK2, ha='right')
fig.tight_layout(); fig.savefig('docs/img/gate_wide.png', facecolor=SURF); plt.close(fig)

# 5. the phases again, measured with the GPU: the first-look score of 64,000 seeds over windows three times as wide
D = np.loadtxt('stats/tailprobe/phase_dump.txt'); ph = D[:, 1:5]; score = D[:, 7] + 0.75 * D[:, 6]
fig, a = frame(7.6, 4.3)
for i, (lab, col, ls) in enumerate((('octave -8, stack A', BLUE, '-'), ('octave -7, stack A', ORANGE, '-'), ('octave -8, stack B', BLUE, (0, (3, 2))), ('octave -7, stack B', ORANGE, (0, (3, 2))))):
    lo, hi = ph[:, i].min(), ph[:, i].max(); e = np.linspace(lo, hi, 15); idx = np.minimum(((ph[:, i] - lo) / (hi - lo) * 14).astype(int), 13)
    a.plot(0.5 * (e[1:] + e[:-1]), [(score[idx == k].mean() - score.mean()) * 1000 for k in range(14)], marker='o', color=col, lw=1.8, ms=4, ls=ls, label=lab)
for x, col in ((0.542, BLUE), (0.584, ORANGE)): a.axvline(x, color=col, lw=1, ls=(0, (1, 2)))
a.invert_yaxis(); a.set_xlabel('where y = 24 sits in the layer\'s cell', color=INK2); a.set_ylabel('mean gate score, in 0.001 from the mean (up = better)', color=INK2)
leg = a.legend(frameon=False, fontsize=8.5, loc='lower center', ncol=2); [t.set_color(INK2) for t in leg.get_texts()]; title(a, 'The best phases, from 64,000 seeds in 45 seconds of GPU')
fig.tight_layout(); fig.savefig('docs/img/phase_fit.png', facecolor=SURF); plt.close(fig)
print('wrote docs/img/exact_rule.png, gate_wide.png, phase_fit.png')
