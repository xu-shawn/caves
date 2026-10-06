#!/usr/bin/env python3
"""Pictures of a cavern too big for one dump: the native terrain is dumped in strips along z (native/nativedump), each
strip is reduced to what the picture needs and deleted. Top view (cave air under each column) and two vertical cuts
through a point.
usage: caveview3.py SEED X0 Z0 SIZE_X SIZE_Z CX CY CZ TITLE OUT.png [THREADS=6] [STRIP=1024]   (X0, Z0, sizes: multiples of 16)"""
import os, subprocess, sys, tempfile, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt, matplotlib.ticker
from matplotlib.colors import ListedColormap
seed = sys.argv[1]; x0, z0, sx, sz, cx, cy, cz = map(int, sys.argv[2:9]); title, out = sys.argv[9], sys.argv[10]
threads = sys.argv[11] if len(sys.argv) > 11 else '6'; strip = int(sys.argv[12]) if len(sys.argv) > 12 else 1024
here = os.path.dirname(os.path.abspath(__file__)); tmp = os.path.join(os.environ.get('CAVE_TMP', tempfile.gettempdir()), f'caveview3_{os.getpid()}.bin')
def classes(block):   # rock 0, cave air 1, water 2, lava 3, open sky 4
    roof = np.maximum.accumulate((block != 1)[..., ::-1], axis=-1)[..., ::-1]
    return np.where(block == 0, 0, np.where(block == 2, 2, np.where(block == 3, 3, np.where(roof, 1, 4)))).astype(np.uint8)
cache = out[:-4] + '.npz'   # the reduced arrays, kept so that the picture can be redrawn without the dump
if os.path.exists(cache): d = np.load(cache); depth, cutx, cutz = d['depth'], d['cutx'], d['cutz']
else:
    depth = np.zeros((sz, sx), np.int32); cutz = np.zeros((sz, 384), np.uint8); cutx = None
    for a in range(0, sz, strip):
        n = min(strip, sz - a); subprocess.run([here + '/native/nativedump', seed, str(x0), str(z0 + a), str(sx), str(n), tmp, threads], check=True, stdout=subprocess.DEVNULL)
        g = np.memmap(tmp, dtype=np.uint8, mode='r', shape=(n, sx, 384))
        for b in range(0, n, 64): depth[a + b:a + b + 64] = (classes(np.asarray(g[b:b + 64])) == 1).sum(axis=2)
        cutz[a:a + n] = classes(np.asarray(g[:, cx - x0]))
        if a <= cz - z0 < a + n: cutx = classes(np.asarray(g[cz - z0 - a]))
        del g; os.remove(tmp); print('strip', a, 'done', flush=True)
    np.savez_compressed(cache, depth=depth.astype(np.int16), cutx=cutx, cutz=cutz)
SURF, INK, INK2 = '#fcfcfb', '#0b0b0b', '#52514e'
cmap = ListedColormap(['#6b6b6b', '#f4e9c8', '#3f76c9', '#e2622a', '#cfe3f5'])
tall = sz > 1.3 * sx; fig = plt.figure(figsize=(15, 10.4 if tall else 9.6), dpi=120); fig.patch.set_facecolor(SURF)
ax = fig.add_axes([0.08, 0.07, 0.42 if tall else 0.46, 0.82])
im = ax.imshow(depth, extent=(x0, x0 + sx, z0 + sz, z0), cmap='magma', interpolation='nearest'); ax.plot([cx], [cz], marker='+', color='white', ms=14, mew=2)
ax.axhline(cz, color='white', lw=0.6, alpha=0.6); ax.axvline(cx, color='white', lw=0.6, alpha=0.6)
ax.set_xlabel('x (blocks)', color=INK2); ax.set_ylabel('z (blocks)', color=INK2); ax.tick_params(colors=INK2, labelsize=9); ax.ticklabel_format(useOffset=False, style='plain'); ax.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(4)); ax.set_title('Cave air under each column (blocks)', loc='left', fontsize=11, color=INK2)
cb = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.02); cb.ax.tick_params(colors=INK2, labelsize=8)
for k, (name, img, lo, lab) in enumerate((('cut along x at z = %d' % cz, cutx.T, x0, 'x'), ('cut along z at x = %d' % cx, cutz.T, z0, 'z'))):
    a = fig.add_axes([0.60, 0.52 - 0.44 * k, 0.38, 0.36]); n = img.shape[1]
    a.imshow(img[:300][::-1], extent=(lo, lo + n, -64, 236), cmap=cmap, vmin=0, vmax=4, interpolation='nearest', aspect='auto')
    a.plot([cx if lab == 'x' else cz], [cy], marker='+', color='black', ms=12, mew=1.5)
    a.set_xlabel(lab + ' (blocks)', color=INK2); a.set_ylabel('y', color=INK2); a.tick_params(colors=INK2, labelsize=9); a.ticklabel_format(useOffset=False, style='plain'); a.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(5)); a.set_title(name, loc='left', fontsize=9.5, color=INK2)
fig.text(0.03, 0.945, title, fontsize=15, fontweight='bold', color=INK); fig.text(0.60, 0.925, 'cuts: grey rock, cream cave air, blue water, orange lava, pale open sky', fontsize=9.5, color=INK2)
fig.savefig(out, facecolor=SURF); print('wrote', out)
