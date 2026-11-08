#!/usr/bin/env python3
"""What icemeasure found: sizes, ice shares, the largest by ice volume and the largest that are ice caves throughout.
usage: look.py out1.txt [out2.txt ...]"""
import sys
import numpy as np
rows = {}
for f in sys.argv[1:]:
    for l in open(f):
        w = l.split()
        if len(w) < 20 or w[11] != 'at' or int(w[3]) == 0: continue
        i = w.index('ice'); key = (w[0], w[5], w[6], w[9], w[10])
        rows[key] = dict(seed=w[0], volume=int(w[3]), ice=int(w[i + 1]), y0=int(w[7]), y1=int(w[8]), dx=int(w[6]) - int(w[5]) + 1, dz=int(w[10]) - int(w[9]) + 1, tp=' '.join(w[12:15]))
r = list(rows.values()); v = np.array([x['volume'] for x in r]); ice = np.array([x['ice'] for x in r]); share = ice / v
print(f'{len(r)} distinct caverns | volume median {int(np.median(v)):,}, 90% {int(np.quantile(v, .9)):,}, largest {int(v.max()):,}')
print(f'ice caves throughout: {int((share == 1).sum())} | over 90%: {int((share > .9).sum())} | over half: {int((share > .5).sum())} | none: {int((ice == 0).sum())}')
print('most ice caves volume in one cavern:')
for x in sorted(r, key=lambda x: -x['ice'])[:10]: print(f"   {x['ice']:10,d} of {x['volume']:10,d} ({100 * x['ice'] / x['volume']:5.1f}%)  y {x['y0']}..{x['y1']}  {x['dx']} x {x['dz']}  seed {x['seed']}  /tp {x['tp']}")
print('largest that are ice caves throughout:')
for x in sorted((x for x in r if x['ice'] == x['volume']), key=lambda x: -x['volume'])[:10]: print(f"   {x['volume']:10,d}  y {x['y0']}..{x['y1']}  {x['dx']} x {x['dz']}  seed {x['seed']}  /tp {x['tp']}")
