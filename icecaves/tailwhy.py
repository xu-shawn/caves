#!/usr/bin/env python3
"""icecaves/tailwhy.py: what bounds the largest ice caverns, from the strict tests of the runs with the new scanner.
Every strict test (tags S and N) of the runs given: a pass has the cavern's volume and box; a fail has how many core
blocks were measured before the core left the ice caves, where, and the climate there.
usage: tailwhy.py [RUN ...]      (default: box1 box2 box3 prod4 prod5)"""
import sys, os, collections
import numpy as np
here = os.path.dirname(os.path.abspath(__file__)); runs = sys.argv[1:] or ['box1', 'box2', 'box3', 'prod4', 'prod5']
T = []
for run in runs:
    p = f'{here}/runs/{run}/results.txt'
    if not os.path.exists(p): continue
    for l in open(p):
        f = l.split()
        if 'seconds' not in f: continue
        j = max(k for k, x in enumerate(f) if x == '|'); tag = f[j + 1:]
        if len(tag) < 10 or tag[0] not in 'SN': continue
        i = f.index('seconds'); v = int(f[3]); left = 'left' in f[i + 4:i + 6]
        d = dict(run=run, seed=f[0], kind=tag[0], v=v, core=int(f[4]), left=left, ice=int(f[i + 3]), sp=int(tag[2]), si=int(tag[3]), so=int(tag[4]), fi=int(tag[6]), fo=int(tag[7]), fp=int(tag[8]), sec=float(f[i + 1]))
        d['score'] = max(d['sp'], d['si'])
        if left:
            k = f.index('left'); d['ex'] = (int(f[k + 2]), int(f[k + 3]), int(f[k + 4])); g = {f[q]: float(f[q + 1]) for q in range(k + 5, j - 1, 2) if f[q] in ('t', 'h', 'c', 'w', 'depth')}; d.update(g)
            far = {'temperature': max(0, g['t'] + 0.8), 'continentalness': max(0, g['c'] + 0.19, -1.05 - g['c']), 'weirdness': max(0, 0.5 - g['w'], g['w'] - 1.1), 'humidity': max(0, g['h'] - 0.7), 'depth': max(0, 0.2 - g['depth'], g['depth'] - 0.9)}
            d['fault'] = max(far, key=far.get)
        else: d['box'] = tuple(int(x) for x in f[5:11])
        T.append(d)
ok = [d for d in T if not d['left'] and d['v'] > 0 and d['ice'] == d['v']]; near = [d for d in T if not d['left'] and d['v'] > 0 and d['ice'] != d['v']]; left = [d for d in T if d['left']]
print(f'{len(T)} strict tests: {len(ok)} ice caves throughout, {len(near)} with rim blocks outside, {len(left)} left the ice caves')
print('\n1. The candidates for a record (scanner cluster of 700+ points): what became of them')
for lo, hi in ((600, 700), (700, 800), (800, 900), (900, 2000)):
    m = [d for d in T if lo <= d['score'] < hi]
    if not m: continue
    a = [d for d in m if d in ok]; b = [d for d in m if d['left']]
    print(f'   {lo}-{hi} points: {len(m):5d} tests; throughout {len(a):4d} ({100 * len(a) / len(m):4.1f}%), median {np.median([d["v"] for d in a]) / 1e6 if a else 0:.2f}M, largest {max([d["v"] for d in a], default=0) / 1e6:.2f}M; rim only {sum(1 for d in m if d in near):4d}; left {len(b):5d}, of them after 2M+ core blocks {sum(1 for d in b if d["core"] >= 2e6):4d}')
print('\n2. Where the core left the ice caves, by the number at fault (all tests that left | those of 700+ points | those that left after 1.5M+ core blocks)')
for name, sel in (('all', left), ('700+ points', [d for d in left if d['score'] >= 700]), ('after 1.5M+ core blocks', [d for d in left if d['core'] >= 1.5e6])):
    c = collections.Counter(d['fault'] for d in sel); n = max(len(sel), 1)
    print(f'   {name:26s} {len(sel):6d}: ' + ', '.join(f'{k} {100 * c[k] / n:.0f}%' for k in ('weirdness', 'continentalness', 'temperature', 'depth', 'humidity')))
print('   by height of the exit (700+ points):')
big = [d for d in left if d['score'] >= 700]
for lo, hi, name in ((-64, -44, 'floor, y -64..-45'), (-44, -24, 'y -44..-25'), (-24, 0, 'y -24..-1'), (0, 24, 'y 0..23'), (24, 200, 'roof, y 24+')):
    m = [d for d in big if lo <= d['ex'][1] < hi]; c = collections.Counter(d['fault'] for d in m)
    if m: print(f'      {name:18s} {100 * len(m) / len(big):4.0f}%: ' + ', '.join(f'{k} {c[k]}' for k, _ in c.most_common()) + f';  weirdness there: median {np.median([d["w"] for d in m]):.2f}, depth {np.median([d["depth"] for d in m]):.2f}, continentalness {np.median([d["c"] for d in m]):.2f}')
print('\n3. The exits by weirdness: how far under the box (0.5), and at what depth')
w = [d for d in big if d['fault'] == 'weirdness']
for lo, hi in ((0.4, 0.5), (0.3, 0.4), (0.2, 0.3), (0.0, 0.2), (-2, 0.0)):
    m = [d for d in w if lo <= d['w'] < hi]
    if m: print(f'   weirdness {lo:+.1f}..{hi:+.1f}: {100 * len(m) / max(len(w), 1):3.0f}%, depth median {np.median([d["depth"] for d in m]):.2f}, y median {np.median([d["ex"][1] for d in m]):.0f}')
print('\n4. The caverns that are ice caves throughout, by size: their box and how full it is')
for lo, hi in ((3.5e6, 4e6), (4e6, 4.5e6), (4.5e6, 5e6), (5e6, 9e6)):
    m = [d for d in ok if lo <= d['v'] < hi]
    if not m: continue
    dx = np.array([d['box'][1] - d['box'][0] for d in m]); dz = np.array([d['box'][5] - d['box'][4] for d in m]); top = np.array([d['box'][3] for d in m]); v = np.array([d['v'] for d in m])
    print(f'   {lo / 1e6:g}-{hi / 1e6:g}M: {len(m):5d}; box {np.median(dx):.0f} x {np.median(dz):.0f} blocks (longer side {np.median(np.maximum(dx, dz)):.0f}, 90%: {np.quantile(np.maximum(dx, dz), .9):.0f}), top y {np.median(top):.0f}; blocks per column of the box {np.median(v / (dx * dz)):.1f}; scanner points {np.median([d["score"] for d in m]):.0f}, formula samples {np.median([max(d["fi"], d["fp"]) for d in m]):.0f}; by the nearly pure test {100 * np.mean([d["kind"] == "N" for d in m]):.0f}%')
