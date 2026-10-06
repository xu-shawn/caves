#!/usr/bin/env python3
"""The search on the curve it is judged by: distinct exact caverns of at least V found per dollar of rented compute, by
pipeline version, with the exponential fitted from 38M up (below that the later versions do not measure every cavern)
and what the fit gives for the largest cavern to expect from a budget.
  old box (RTX A2000 + 61 CPU threads, $0.159 an hour): run g10 (percolation gate), run g12 (CPU coarse looks)
  new box (RTX 4080 Super + 61 CPU threads, $0.295 an hour): run g13 by stretch of its settings (stats/now/g13/settings.txt)
usage: fit_dollar.py OUT.png [G13_DIR=stats/now/g13] [G13_END] [G14_DIR G14_END]      (G14: the third rental's run, optional)
the final lists are kept in stats/now/g13 and stats/now/g14: fit_dollar.py stats/fit_dollar.png stats/now/g13 2026-10-05T22:17:20Z stats/now/g14 2026-10-06T02:07:26Z
old usage: fit_dollar.py OUT.png [G13_DIR=stats/now/g13]      (data: stats/now/g*/seeds.txt, G13_DIR/{seeds,s3,settings}.txt and pulled_at.txt or the
last line of gate_first.txt for the end time, hits/cv1_s3.txt). Also writes stats/dollar_table.md for the write-up."""
import sys, os, re, calendar, time, collections, math, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
here = os.path.dirname(os.path.abspath(__file__)); out = sys.argv[1]; g13 = sys.argv[2] if len(sys.argv) > 2 else f'{here}/now/g13'
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'
def stamp(s): return calendar.timegm(time.strptime(s, '%Y-%m-%dT%H:%M:%SZ'))
old = collections.defaultdict(set)
for l in open(f'{here}/../hits/cv1_s3.txt'):
    w = l.split()
    if len(w) > 14 and int(w[3]) > 0: old[w[0]].add(int(w[3]))
def old_run(run): seeds = [l.split()[0] for l in open(f'{here}/now/{run}/seeds.txt') if l.strip()]; return [v for s in seeds for v in old.get(s, ())], len(seeds)
series = []
v, n = old_run('g10'); series.append(('old box: percolation gate (08:20)', v, 1.80, 0.159, '#d4a017', n))
v, n = old_run('g12'); series.append(('old box: coarse looks on CPU threads (10:42)', v, 1.90, 0.159, '#9b59b6', n))
seeds = [l.split()[0] for l in open(f'{g13}/seeds.txt') if l.strip()]; caves = collections.defaultdict(set)
for l in open(f'{g13}/s3.txt'):
    w = l.split()
    if len(w) > 14 and int(w[3]) > 0: caves[w[0]].add(int(w[3]))
marks = []
for l in open(f'{g13}/settings.txt'):
    m = re.search(r'from seed line (\d+)', l)
    if m: marks.append((stamp(l.split()[0]), int(m.group(1)), l))
end = stamp(sys.argv[3]) if len(sys.argv) > 3 else stamp(open(f'{g13}/pulled_at.txt').read().strip())   # the time the lists end: given (a final download: when the search was stopped), or the time of the pull
def stretch(t0, t1):   # the seeds scanned between two times, by the marks' seed lines (the nearest marks inside)
    inside = [m for m in marks if t0 <= m[0] <= t1]; a = inside[0][1]; b = inside[-1][1] if inside[-1][0] < t1 - 1 and t1 < end else len(seeds)
    if t1 < end: b = min((m[1] for m in marks if m[0] >= t1), default=len(seeds))
    return [v for s in seeds[a:b] for v in caves.get(s, ())], b - a
for label, t0, t1, colour in (('second box: gate on the GPU, workers as before (17:24)', '2026-10-05T17:24:08Z', '2026-10-05T17:37:27Z', '#2a78d6'),
                              ('second box: scanner and exact measure on the GPU too (19:15)', '2026-10-05T19:15:32Z', '2026-10-05T19:58:00Z', '#eb6834'),
                              ('second box: fewer round trips, vectorised measurer, CPU-look threads (20:48)', '2026-10-05T20:48:00Z', None, '#3a9a5b')):
    a = stamp(t0); b = stamp(t1) if t1 else end
    if a >= end: continue
    v, n = stretch(a, min(b, end)); series.append((label, v, (min(b, end) - a) / 3600, 0.295, colour, n))
v = [x for s in seeds for x in caves.get(s, ())]; whole = ('second box: the whole run', v, (end - marks[0][0]) / 3600, 0.295, None, len(seeds))
third = None
if len(sys.argv) > 5:   # the third rental's run: one build, its seeds and caverns in their own lists
    d, end14 = sys.argv[4], stamp(sys.argv[5]); seeds14 = [l.split()[0] for l in open(f'{d}/seeds.txt') if l.strip()]; caves14 = collections.defaultdict(set)
    for l in open(f'{d}/s3.txt'):
        w = l.split()
        if len(w) > 14 and int(w[3]) > 0: caves14[w[0]].add(int(w[3]))
    start14 = stamp(open(f'{d}/settings.txt').readline().split()[0])
    third = ('third rental: window sums with the z kernel made once, chain scan (23:10)', [x for s in seeds14 for x in caves14.get(s, ())], (end14 - start14) / 3600, 0.295, '#0b0b0b', len(seeds14))
    series.append(third)
    # its last stretch on its own (from the last restart of the workers: the exact rule that follows the gate score), for the table only
    last14 = [(stamp(l.split()[0]), int(re.search(r'from seed line (\d+)', l).group(1))) for l in open(f'{d}/settings.txt') if ' workers: ' in l and 'from seed line' in l][-1]
    tail14 = ('third rental: its last stretch', [x for s in seeds14[last14[1]:] for x in caves14.get(s, ())], (end14 - last14[0]) / 3600, 0.295, None, len(seeds14) - last14[1])
fig, ax = plt.subplots(figsize=(10.5, 6.2), dpi=150); fig.patch.set_facecolor(SURF); ax.set_facecolor(SURF); ax.set_axisbelow(True); ax.tick_params(colors=MUTED, length=0); ax.grid(True, color=GRID, lw=0.8)
for s in ('top', 'right'): ax.spines[s].set_visible(False)
for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
T = np.arange(34, 56.01, 1.0); BASE = 40.0
# One slope for every version: the versions differ in how many caverns they find per dollar, not in how the caverns'
# sizes fall off (their separate slopes scatter from 4.3M to 5.4M, within their errors). Maximum likelihood for an
# exponential tail over all the caverns of at least 40M of the three runs: a factor 10 per ln(10) x mean(V - 40M).
pool = np.array([v / 1e6 for run in (old_run('g10')[0], old_run('g12')[0], [v for s in seeds for v in caves.get(s, ())], third[1] if third else []) for v in run if v >= BASE * 1e6])
decade = math.log(10) * (pool.mean() - BASE); decade_err = decade / math.sqrt(len(pool))
print(f'pooled slope from {len(pool)} caverns of at least {BASE:.0f}M: a factor 10 per {decade:.2f}M +- {decade_err:.2f}M')
levels = []; table = ['| build | hours | cost | 38M+ | 40M+ | 42M+ | 44M+ | 46M+ | largest to expect for $1 | for $10 |', '|------------------------------|---|---|---|---|---|---|---|-----|---|']
SHORT = {'old box: percolation gate (08:20)': 'box 1, percolation gate', 'old box: coarse looks on CPU threads (10:42)': 'box 1, last build (CPU coarse looks)', 'second box: gate on the GPU, workers as before (17:24)': 'box 2, gate on the GPU', 'second box: scanner and exact measure on the GPU too (19:15)': 'box 2, workers on the GPU too', 'second box: fewer round trips, vectorised measurer, CPU-look threads (20:48)': 'box 2, last build', 'second box: the whole run': 'box 2, whole run', 'third rental: window sums with the z kernel made once, chain scan (23:10)': 'box 3, z kernel made once (all of it)', 'third rental: its last stretch': 'box 3, its last 75 minutes'}
for label, vols, hours, price, colour, n in [x for x in series if not third or x[0] != third[0]] + [whole] + ([third] if third else []) + ([tail14] if third and tail14[2] > 0.2 else []):   # (the table's order: box by box)
    dollars = hours * price; counts = np.array([sum(v >= t * 1e6 for v in vols) for t in T]); m = counts > 0
    if colour: ax.errorbar(T[m], counts[m] / dollars, yerr=[np.minimum(np.sqrt(counts[m]), counts[m] * 0.9) / dollars, np.sqrt(counts[m]) / dollars], fmt='o', ms=5, color=colour, ecolor=colour, elinewidth=1, capsize=2, zorder=3)
    k = int(sum(v >= BASE * 1e6 for v in vols)); level = k / dollars   # caverns of at least 40M per dollar: the version's place on the curve
    xs = np.array([37.0, 58.0])
    if colour: ax.plot(xs, level * 10 ** (-(xs - BASE) / decade), color=colour, lw=2.2, label=f'{label}  ({hours:.2f} h, ${dollars:.2f})')
    expect = lambda budget: BASE + decade * math.log10(level * budget)   # where the rate x the budget is one cavern
    spread = decade * math.log10(1 + 1 / math.sqrt(k))
    print(f'{label:58s} {hours:5.2f} h ${dollars:.3f} {n:6d} seeds; per dollar: ' + ', '.join(f'{t:.0f}M+ {c / dollars:.0f}' for t, c in zip(T, counts) if t in (38, 40, 42, 44, 46, 50)) + f'; largest to expect for $1: {expect(1):.1f}M, $10: {expect(10):.1f}M, $100: {expect(100):.1f}M (+- {spread:.1f}M from the count of {k})')
    levels.append((label, level, k))
    table.append(f'| {SHORT.get(label, label)} | {hours:.2f} | ${dollars:.2f} | ' + ' | '.join(f'{counts[list(T).index(t)] / dollars:,.0f}' for t in (38, 40, 42, 44, 46)) + f' | {expect(1):.1f}M ± {spread:.1f} | {expect(10):.1f}M |')
if third:   # the last build's numbers for the write-up's text
    import json; lv = [x for x in levels if x[0] == third[0]][0]; d3 = third[2] * third[3]; n46 = sum(v >= 46e6 for v in third[1])
    json.dump({'factor': f'{lv[1] / levels[1][1]:.1f}', 'rate': f'{lv[1]:.0f}', 'shift': f'{decade * math.log10(lv[1] / levels[1][1]):+.1f}M', 'r46': f'{n46 / d3:.0f}', 'n46': str(n46)}, open(os.path.join(here, 'dollar_last.json'), 'w'))
for label, level, k in levels[1:]: print(f'  {label[:50]:50s} x{level / levels[1][1]:.2f} of the old box\'s last run: {decade * math.log10(level / levels[1][1]):+.2f}M on the largest cavern of any budget')
ax.set_yscale('log'); ax.set_xlim(33.5, 58.5); ax.set_ylim(0.3, 2e4); ax.set_xlabel('cavern volume V, million blocks (exact, 7-block rule)', color=INK2); ax.set_ylabel('distinct caverns of at least V found per dollar', color=INK2)
ax.set_title('Caverns per dollar of compute, by pipeline version', loc='left', fontsize=13, color=INK, fontweight='bold')
ax.text(0.995, 0.97, f'lines: one slope for all (a factor 10 per {decade:.1f}M),\neach version at its own count of 40M+ caverns', transform=ax.transAxes, ha='right', va='top', fontsize=9, color=INK2)
leg = ax.legend(loc='lower left', frameon=False, fontsize=9); [t.set_color(INK2) for t in leg.get_texts()]
fig.tight_layout(); fig.savefig(out, facecolor=SURF)
open(os.path.join(here, 'dollar_table.md'), 'w').write('\n'.join(table) + f'\n\n(Distinct caverns of at least that volume per dollar of rented box. Largest to expect: where the fitted rate times the budget is one cavern, with one slope for all builds, a factor 10 per {decade:.1f}M ± {decade_err:.1f}M from {len(pool)} caverns of 40M+; the ± is from the build\'s own count of 40M+ caverns.)\n')
