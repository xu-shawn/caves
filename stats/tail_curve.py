#!/usr/bin/env python3
"""The tail curve of the cave search, box by box, and what it says a budget buys.
Left: distinct caverns of at least V found per dollar of rented box (exact volumes), with the exponential tail fitted by
maximum likelihood from 40M up (a factor 10 per ln(10) x mean(V - 40M)). The boxes' own slopes agree within their
errors, so the lines share the pooled slope and each box keeps its own level (its count of 40M+ caverns per dollar).
Right: the largest cavern to expect from a budget, V*(B) = 40M + slope x log10(level x B), the size at which the fitted
rate times the budget is one cavern; the band is where the largest cavern of a run of the last build falls 8 times in
10 (the largest of a Poisson number of exponential tails: V* + slope x log10(1 / -ln p) for p = 0.1 and 0.9); the
points are what each run actually found for what it cost.
  box 1  RTX A2000 + 61 CPU threads, $0.159 an hour: its last run (g12)
  box 2  RTX 4080 Super + 61 CPU threads, $0.295 an hour: its whole run (g13)
  box 3  the same machine, with the window sums restructured: its whole run (g14)
Data: stats/now/g12, g13, g14 (seeds.txt, s3.txt, settings.txt), hits/cv1_s3.txt.   usage: tail_curve.py [OUT=stats/tail_curve.png]"""
import sys, os, math, calendar, time, collections, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
here = os.path.dirname(os.path.abspath(__file__)); out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, 'tail_curve.png')
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'
COLOURS = ('#2a78d6', '#eb6834', '#9b59b6')   # checked with the dataviz validator (light surface): all six checks pass
BASE = 40.0
def stamp(s): return calendar.timegm(time.strptime(s, '%Y-%m-%dT%H:%M:%SZ'))
def volumes(seeds_file, s3_file):   # the exact caverns of the seeds a run scanned, a cavern once per seed
    seeds = {l.split()[0] for l in open(seeds_file) if l.strip()}; found = collections.defaultdict(set)
    for l in open(s3_file):
        w = l.split()
        if len(w) > 14 and w[0] in seeds and int(w[3]) > 0: found[w[0]].add(int(w[3]))
    return np.array([v / 1e6 for s in found.values() for v in s])
def first_mark(run): return min(stamp(l.split()[0]) for l in open(f'{here}/now/{run}/settings.txt') if 'from seed line' in l)
runs = [('box 1, last build', volumes(f'{here}/now/g12/seeds.txt', f'{here}/../hits/cv1_s3.txt'), 1.90 * 0.159),
        ('box 2, whole run', volumes(f'{here}/now/g13/seeds.txt', f'{here}/now/g13/s3.txt'), (stamp('2026-10-05T22:17:20Z') - first_mark('g13')) / 3600 * 0.295),
        ('box 3, whole run', volumes(f'{here}/now/g14/seeds.txt', f'{here}/now/g14/s3.txt'), (stamp('2026-10-06T02:07:26Z') - first_mark('g14')) / 3600 * 0.295)]
pool = np.concatenate([v[v >= BASE] for _, v, _ in runs]); slope = math.log(10) * (pool.mean() - BASE); slope_err = slope / math.sqrt(len(pool))
print(f'pooled: {len(pool)} caverns of {BASE:.0f}M+, a factor 10 per {slope:.2f}M +- {slope_err:.2f}M')

fig, (ax, bx) = plt.subplots(1, 2, figsize=(13.2, 5.9), dpi=150, gridspec_kw={'width_ratios': [1.08, 1]}); fig.patch.set_facecolor(SURF)
for a in (ax, bx):
    a.set_facecolor(SURF); a.set_axisbelow(True); a.tick_params(colors=MUTED, length=0); a.grid(True, color=GRID, lw=0.8)
    for s in ('top', 'right'): a.spines[s].set_visible(False)
    for s in ('left', 'bottom'): a.spines[s].set_color(GRID)
T = np.arange(38, 57.01, 1.0); budgets = np.logspace(-1, 3, 200); rows = []
for (name, v, dollars), colour in zip(runs, COLOURS):
    counts = np.array([(v >= t).sum() for t in T]); m = counts > 0; tail = v[v >= BASE]; level = len(tail) / dollars
    own = math.log(10) * (tail.mean() - BASE); own_err = own / math.sqrt(len(tail))
    ax.errorbar(T[m], counts[m] / dollars, yerr=[np.minimum(np.sqrt(counts[m]), counts[m] * 0.9) / dollars, np.sqrt(counts[m]) / dollars], fmt='o', ms=4.5, color=colour, ecolor=colour, elinewidth=1, capsize=2, zorder=3)
    xs = np.array([BASE, 60.0]); ax.plot(xs, level * 10 ** (-(xs - BASE) / slope), color=colour, lw=2, label=f'{name}: {level:,.0f} of 40M+ per dollar\n(its own slope: a factor 10 per {own:.1f}M ± {own_err:.1f})')
    expect = BASE + slope * np.log10(level * budgets); bx.plot(budgets, expect, color=colour, lw=2)
    if name.startswith('box 3'): bx.fill_between(budgets, expect + slope * math.log10(1 / -math.log(0.1)), expect + slope * math.log10(1 / -math.log(0.9)), color=colour, alpha=0.13, lw=0, zorder=1)
    bx.plot([dollars], [v.max()], 'o', ms=8, color=colour, markeredgecolor=SURF, markeredgewidth=2, zorder=4)
    at1 = BASE + slope * math.log10(level); rows.append((name, dollars, len(v), len(tail), level, own, own_err, v.max(), at1))
    print(f'{name}: ${dollars:.2f}; {len(tail)} caverns of 40M+ = {level:.0f} per dollar; own slope a factor 10 per {own:.2f}M +- {own_err:.2f}; largest found {v.max():.2f}M; largest to expect for $1 {at1:.1f}M, $10 {at1 + slope:.1f}M, $100 {at1 + 2 * slope:.1f}M; what the fit expects for its own cost: {BASE + slope * math.log10(level * dollars):.1f}M')
ax.set_yscale('log'); ax.set_xlim(37.5, 57.5); ax.set_ylim(0.25, 4000); ax.set_xlabel('cavern volume V, million blocks (exact)', color=INK2); ax.set_ylabel('distinct caverns of at least V found per dollar', color=INK2)
ax.set_title('The tail: caverns per dollar', loc='left', fontsize=13, color=INK, fontweight='bold')
ax.text(0.015, 0.03, f'lines: one slope for all, a factor 10 per {slope:.2f}M ± {slope_err:.2f}\n({len(pool):,} caverns of 40M+), each box at its own level', transform=ax.transAxes, ha='left', va='bottom', fontsize=9, color=INK2)
leg = ax.legend(loc='upper right', frameon=False, fontsize=9, handlelength=1.6, labelspacing=0.9); [t.set_color(INK2) for t in leg.get_texts()]
ax.set_xticks(range(38, 58, 2))
# direct labels on the right panel: the lines at their right end, the points by what they are
labels = {'box 1, last build': ((0.30, 49.6), 'box 1: 50.99M for $0.30'), 'box 2, whole run': ((1.9, 53.55), 'box 2: 54.58M for $1.46'), 'box 3, whole run': ((0.36, 56.55), 'box 3: 55.83M for $0.87')}
for (name, dollars, n, n40, level, own, own_err, largest, at1), colour in zip(rows, COLOURS):
    (x, y), text = labels[name]; bx.text(x, y, text, color=INK2, fontsize=9, ha='left' if name != 'box 1, last build' else 'left', va='center')
    bx.text(1150, BASE + slope * math.log10(level * 1000) + {'box 1': -0.55, 'box 2': 0.1, 'box 3': 0.35}[name.split(',')[0]], name.split(',')[0], color=INK2, fontsize=9, va='center', ha='left', clip_on=False)
bx.text(0.125, 68.6, 'lines: the size at which rate × budget is one cavern\nshaded: where the largest cavern of a run of the\nbox 3 build falls, 8 times in 10', color=INK2, fontsize=9, ha='left', va='top')
bx.set_xscale('log'); bx.set_xlim(0.1, 1000); bx.set_ylim(46, 70); bx.set_xlabel('budget, dollars of rented box', color=INK2); bx.set_ylabel('largest cavern to expect, million blocks', color=INK2)
bx.set_xticks([0.1, 1, 10, 100, 1000]); bx.set_xticklabels(['$0.10', '$1', '$10', '$100', '$1,000'])
bx.set_title('What a budget buys', loc='left', fontsize=13, color=INK, fontweight='bold')
fig.tight_layout(w_pad=2.5); fig.subplots_adjust(right=0.955); fig.savefig(out, facecolor=SURF); print('wrote', out)
