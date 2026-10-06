#!/usr/bin/env python3
"""The rarity curves per box-hour from exact caverns, for the pipeline versions since the exact measure follows the
open volume as well as the formula cluster (run from anywhere: fit_exact.py OUT.png).
  v2 - v4   as in fit_now.py (tail.py's weighted counts)
  v6        run g5 from seed line 13025 (04:20:52) to its end (05:40:19): 512-block gate, early exit; its sites were
            measured by the search and, for the ones the old rule skipped, afterwards on the laptop (hits/backfill_s3.txt)
  v7        run g6 (05:40:19 - 06:05:32): the wide seed gate
  v8        run g7 (06:05:50 - 06:47:33): wide + 0.75 x 512-block gate + terrain, faster gate
  v9        run g8 (06:47:48 - 07:03:31): the phase scan on the GPU, terrain score <= -0.75
  v10       run g9 to seed line 8605 (07:03:46 - 07:28:23): the first look at the tables on the GPU too
  v12       run g10 (from 08:20:14): seedgate8, the gate score minus 0.0912 x the percolation volume (percgate.hpp)
  v11       run g9 from there (to 08:19:59): phase windows from the GPU sample, table kernel v3, 200K pairs, stop threshold 292 (07:48), exact for F + 2 O >= 102M
  v13       run g11 (10:08:10 - 10:42:11): seedgate9, a cheap look on the GPU before the first look, wider phase and terrain filters
  v14       run g12 (from 10:42:11): seedgate10, the cheap looks on CPU threads in 16-bit arithmetic (six times the seeds looked at), thread counts within the CPU quota, scan kernel in two passes
The lines of v7, v9 and v11 are not drawn (the table has them).
Seed lists: stats/now/g*/seeds.txt (pulled from the box); exact caverns: hits/*.txt. A cavern counts once per seed."""
import sys, os, time, calendar, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here); import tail
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'
out = sys.argv[1]; now = sys.argv[2] if len(sys.argv) > 2 else time.strftime('%FT%TZ', time.gmtime())   # the time the seed lists were pulled
def stamp(s): return calendar.timegm(time.strptime(s, '%Y-%m-%dT%H:%M:%SZ'))
def seeds_of(run, first=0, last=None):
    path = f'{here}/now/{run}/seeds.txt'
    return {w[0] for w in [l.split() for l in open(path)][first:last] if len(w) >= 9} if os.path.exists(path) else set()
exact = {}
for f in ('cv1_s3.txt', 'local_probe_s3.txt', 'backfill_s3.txt'):
    for l in open(f'{here}/../hits/{f}'):
        w = l.split()
        if len(w) > 14 and float(w[3]) > 0: exact.setdefault(w[0], set()).add(int(w[3]))
def counts(seeds, T): v = np.array([a / 1e6 for s in seeds for a in exact.get(s, ())]); return np.array([np.sum(v >= t) for t in T]), (v.max() if len(v) else 0)
def span(a, b): return (stamp(b) - stamp(a)) / 3600
new = [('v6 04:21  early exit on the terrain cut; exact measure by cluster and open volume', seeds_of('g5', 13025), span('2026-10-05T04:20:52Z', '2026-10-05T05:40:19Z'), '#2a78d6'),
       ('v7 05:40  seed gate on 1280-block windows', seeds_of('g6'), span('2026-10-05T05:40:19Z', '2026-10-05T06:05:32Z'), '#eb6834'),
       ('v8 06:06  gate on both window sizes', seeds_of('g7'), span('2026-10-05T06:05:50Z', '2026-10-05T06:47:33Z'), '#3a9a5b'),
       ('v9 06:48  seed scan on the GPU, deeper terrain filter', seeds_of('g8'), span('2026-10-05T06:47:48Z', '2026-10-05T07:03:31Z'), '#898781'),
       ('v10 07:04  first look at the tables on the GPU', seeds_of('g9', 0, 8605), span('2026-10-05T07:03:46Z', '2026-10-05T07:28:23Z'), '#c0392b'),
       ('v11 07:28  phase windows fitted on the GPU, kernel 2.5x faster, leaner workers', seeds_of('g9', 8605), span('2026-10-05T07:28:23Z', '2026-10-05T08:19:59Z'), '#898781'),
       ('v12 08:20  percolation term in the gate (connected low-noise volume)', seeds_of('g10'), span('2026-10-05T08:20:14Z', '2026-10-05T10:08:10Z'), '#d4a017'),
       ('v13 10:08  cheap look on the GPU before the first look', seeds_of('g11'), span('2026-10-05T10:08:10Z', '2026-10-05T10:42:11Z'), '#9b59b6'),
       ('v14 10:42  cheap looks on CPU threads, six times the seeds looked at', seeds_of('g12'), span('2026-10-05T10:42:11Z', now), '#0b0b0b')]
DRAWN = ('v6', 'v8', 'v10', 'v12', 'v13', 'v14')
FROM = {'v14': 38}   # v14 measures a cavern only if F + 2 O >= 107M (102M before), which leaves out most caverns under 38M: its counts under that are not comparable, and its line is fitted from 38M
fig, ax = plt.subplots(figsize=(10.5, 6.6), dpi=150); fig.patch.set_facecolor(SURF); ax.set_facecolor(SURF); ax.set_axisbelow(True); ax.tick_params(colors=MUTED, length=0); ax.grid(True, color=GRID, lw=0.8)
for s in ('top', 'right'): ax.spines[s].set_visible(False)
for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
T = np.arange(22, 40.01, 1.0); lines = []
for label, d, rate, lo, where in (('v2 00:55', 'v2', 615, 24, 25.0), ('v3 02:05', 'v3', 1406, 26, 27.0), ('v4 02:41  phase gate', 'v4', 1650, 30, 31.0)):   # the earlier versions, grey: context
    seeds, distinct, rows = tail.load(os.path.join(here, d)); n, e = tail.tail(rows, seeds, T); m = (T >= lo) & (n > 0); a, b = tail.fit(T[m], n[m], e[m])
    xs = np.array([lo, 46.0]); ax.plot(xs, 10 ** (a + b * xs) * rate, color=MUTED, lw=1.3); ax.plot(T[m], n[m] * rate, 'o', ms=3, color=MUTED)
    ax.text(where, 10 ** (a + b * where) * rate * 1.25, label, color=MUTED, fontsize=8.5, rotation=-33, rotation_mode='anchor'); lines.append((label, a + np.log10(rate), b, None))
Tn = np.arange(28, 52.01, 1.0)
for label, seeds, hours, c in new:
    if hours <= 0 or not seeds: continue
    n, top = counts(seeds, Tn); m = n > 0
    if not m.any(): continue
    drawn = label.split()[0] in DRAWN
    if drawn: ax.errorbar(Tn[m], n[m] / hours, yerr=[np.minimum(np.sqrt(n[m]), n[m] * 0.9) / hours, np.sqrt(n[m]) / hours], fmt='o', ms=5, color=c, ecolor=c, elinewidth=1, capsize=2, zorder=3)
    lo = FROM.get(label.split()[0], 30); f = m & (Tn >= lo) & (n >= 3); b, a = np.polyfit(Tn[f], np.log10(n[f] / hours), 1, w=np.sqrt(n[f])) if f.sum() >= 3 else (np.nan, np.nan)
    if f.sum() >= 3 and drawn: xs = np.array([lo - 1.0, 52.0]); ax.plot(xs, 10 ** (a + b * xs), color=c, lw=2.2, label=f'{label}  ({hours:.2f} h, {len(seeds):,} seeds)' + (f'; fit from {lo}M' if lo != 30 else ''))
    lines.append((label, a, b, (hours, len(seeds), n, top)))
best = max(a / 1e6 for v in exact.values() for a in v); ax.axvline(best, color=INK2, lw=1, ls=(0, (3, 3))); ax.text(best - 0.15, 3e3, f'this box\'s largest {best:.2f}M', color=INK2, fontsize=9, ha='right', va='top')
ax.set_yscale('log'); ax.set_xlim(23.5, 52.5); ax.set_ylim(5e-3, 5e3); ax.set_xlabel('cavern volume V, million blocks (exact, 7-block rule)', color=INK2); ax.set_ylabel('distinct caverns of at least V found per box-hour ($0.155)', color=INK2)
ax.set_title('What the first box found per hour, by pipeline version (exact caverns)', loc='left', fontsize=13, color=INK, fontweight='bold')
leg = ax.legend(loc='lower left', frameon=False, fontsize=9); [t.set_color(INK2) for t in leg.get_texts()]
fig.tight_layout(); fig.savefig(out, facecolor=SURF)
for label, a, b, extra in lines:
    print(f'{label[:44]:44s} log10(per hour) = {a:6.2f} {b:+.3f} V (decade per {-1 / b:.2f}M): ' + ', '.join(f'>={t}M: {10 ** (a + b * t):.3g}/h' for t in (30, 34, 38, 42, 44.3)))
    if extra: hours, ns, n, top = extra; print(f'      {hours:.2f} h, {ns} seeds ({ns / hours:.0f}/h); counted: ' + ', '.join(f'>={int(t)}M: {int(k)} ({k / hours:.3g}/h)' for t, k in zip(Tn, n) if t in (30, 34, 36, 38, 40, 42, 44, 46)) + f'; largest {top:.2f}M')
import json   # the counts, for the table of the write-up (docs/build_rates.py)
json.dump([{'label': label, 'hours': extra[0], 'seeds': extra[1], 'decade': -1 / b, 'largest': float(extra[3]), 'counts': {str(int(t)): int(k) for t, k in zip(Tn, extra[2])}} for label, a, b, extra in lines if extra], open(os.path.join(here, 'fit_exact.json'), 'w'), indent=1)
