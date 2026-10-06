#!/usr/bin/env python3
"""Pictures of a cavern from a CaveDump file: how much cave air each column holds (top view) and two vertical cuts
through the cavern's centre. usage: caveview.py DUMP X0 Z0 SIZE_X SIZE_Z CX CY CZ TITLE OUT.png"""
import sys, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap
dump, x0, z0, sx, sz, cx, cy, cz, title, out = sys.argv[1], *map(int, sys.argv[2:9]), sys.argv[9], sys.argv[10]
g = np.fromfile(dump, dtype=np.uint8).reshape(sz, sx, 384)
roof = np.cumsum((g != 1)[:, :, ::-1], axis=2)[:, :, ::-1] > 0
cls = np.where(g == 0, 0, np.where(g == 2, 2, np.where(g == 3, 3, np.where(roof, 1, 4)))).astype(np.uint8)   # rock, cave air, water, lava, open sky
SURF, INK, INK2 = '#fcfcfb', '#0b0b0b', '#52514e'
cmap = ListedColormap(['#6b6b6b', '#f4e9c8', '#3f76c9', '#e2622a', '#cfe3f5'])
fig = plt.figure(figsize=(15, 9.6), dpi=120); fig.patch.set_facecolor(SURF)
ax = fig.add_axes([0.05, 0.08, 0.50, 0.80]); depth = (cls == 1).sum(axis=2)
im = ax.imshow(depth, extent=(x0, x0 + sx, z0 + sz, z0), cmap='magma', interpolation='nearest'); ax.plot([cx], [cz], marker='+', color='white', ms=14, mew=2)
ax.axhline(cz, color='white', lw=0.6, alpha=0.6); ax.axvline(cx, color='white', lw=0.6, alpha=0.6)
ax.set_xlabel('x (blocks)', color=INK2); ax.set_ylabel('z (blocks)', color=INK2); ax.tick_params(colors=INK2, labelsize=9); ax.set_title('Cave air under each column (blocks)', loc='left', fontsize=11, color=INK2)
cb = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.02); cb.ax.tick_params(colors=INK2, labelsize=8)
for k, (name, img, lo, lab) in enumerate((('cut along x at z = %d' % cz, cls[cz - z0, :, :].T, x0, 'x'), ('cut along z at x = %d' % cx, cls[:, cx - x0, :].T, z0, 'z'))):
    a = fig.add_axes([0.62, 0.52 - 0.44 * k, 0.36, 0.36]); n = img.shape[1]
    a.imshow(img[:260][::-1], extent=(lo, lo + n, -64, 196), cmap=cmap, vmin=0, vmax=4, interpolation='nearest', aspect='auto')
    a.plot([cx if lab == 'x' else cz], [cy], marker='+', color='black', ms=12, mew=1.5)
    a.set_xlabel(lab + ' (blocks)', color=INK2); a.set_ylabel('y', color=INK2); a.tick_params(colors=INK2, labelsize=9); a.set_title(name, loc='left', fontsize=9.5, color=INK2)
fig.text(0.05, 0.94, title, fontsize=15, fontweight='bold', color=INK); fig.text(0.62, 0.905, 'cuts: grey rock, cream cave air, blue water, orange lava, pale open sky', fontsize=9.5, color=INK2)
fig.savefig(out, facecolor=SURF); print('wrote', out)
