#!/usr/bin/env python3
"""Block-for-block comparison of two terrain dumps (CaveDump format): compare.py A B SIZE_X SIZE_Z"""
import sys, numpy as np
sx, sz = int(sys.argv[3]), int(sys.argv[4])
a = np.fromfile(sys.argv[1], dtype=np.uint8).reshape(sz, sx, 384); b = np.fromfile(sys.argv[2], dtype=np.uint8).reshape(sz, sx, 384)
d = a != b; names = ('solid', 'air', 'water', 'lava')
print(f'{a.size} blocks, {int(d.sum())} different;  air {int((b == 1).sum())}, water {int((b == 2).sum())}, lava {int((b == 3).sum())} in the second')
if d.any():
    for code, name in enumerate(names):
        row = {n: int(((b == code) & (a == c)).sum()) for c, n in enumerate(names) if c != code and ((b == code) & (a == c)).any()}
        if row: print(f'   second {name}, first says', row)
    z, x, y = np.nonzero(d); print('   y range', y.min() - 64, y.max() - 64, '; first (x, y, z offsets):', [(int(x[i]), int(y[i]) - 64, int(z[i])) for i in range(min(4, len(z)))])
