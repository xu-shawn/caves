#!/usr/bin/env python3
"""icecaves/abgate.py: runs with the same pipeline and different gates, side by side.
Per run: seeds, thread-s a seed, and per seed (with the Poisson error) the strict tests of pure formula clusters of 550+
samples, and the caverns that are ice caves throughout of 3.5M+ and 4M+ found by the test of pure clusters (tag S: the
same policy in every run). usage: abgate.py RUN [RUN ...]"""
import math, os, sys
here = os.path.dirname(os.path.abspath(__file__))
for run in sys.argv[1:]:
    n = 0; t = 0.0
    for l in open(f'{here}/runs/{run}/seeds.log'):
        f = l.split()
        if len(f) >= 30: n += 1; t += float(f[2]) + float(f[4]) + float(f[6])
    c550 = 0; seen = set(); c35 = c4 = c45 = 0
    for l in open(f'{here}/runs/{run}/results.txt'):
        f = l.split()
        if 'seconds' not in f: continue
        j = max(k for k, x in enumerate(f) if x == '|'); tag = f[j + 1:]
        if len(tag) < 10 or tag[0] != 'S': continue
        i = f.index('seconds'); v = int(f[3]); ok = not ('left' in f[i + 4:i + 6] or v == 0 or int(f[i + 3]) != v)
        if int(tag[8]) >= 550: c550 += 1
        if ok and (f[0], v) not in seen: seen.add((f[0], v)); c35 += v >= 3.5e6; c4 += v >= 4e6; c45 += v >= 4.5e6
    r = lambda c: f'{c / n:.4f} +- {math.sqrt(max(c, 1)) / n:.4f}'
    print(f'{run}: {n} seeds, {t / n:.1f} thread-s; clusters of 550+ {r(c550)}; caverns 3.5M+ {r(c35)}, 4M+ {r(c4)}, 4.5M+ {c45}')
