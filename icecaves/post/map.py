#!/usr/bin/env python3
"""icecaves/post/map.py: a map of one cavern for a post: 1920 x 1080, no title.
Input: a dump of the cavern's blocks (`echo "SEED X Y Z" | CAVE_DUMP=post/NAME.bin ./icemeasure 1`: a record per column,
x, z, then the column's 320 bits from y -64 up). The map shows the cavern's blocks per column. A cavern longer in z than
in x is turned a quarter (north to the left) so that it fills the frame; the arrow says which way north is.
usage: post/map.py DUMP OUT.png [TP_X TP_Z]"""
import sys, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
SURF, INK, MUTED = '#fcfcfb', '#0b0b0b', '#898781'
BLUES = LinearSegmentedColormap.from_list('blues', ['#dbe8f8', '#9dbfee', '#2a78d6', '#0c2f66', '#061a3d'])   # one hue, light to dark: more blocks, darker
rec = np.fromfile(sys.argv[1], dtype=np.dtype([('x', '<i4'), ('z', '<i4'), ('w', '<u8', 5)])); out = sys.argv[2]; tp = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else None
count = np.unpackbits(rec['w'].view(np.uint8).reshape(len(rec), 40), axis=1, bitorder='little').sum(1).astype(float)
x0, x1, z0, z1 = int(rec['x'].min()), int(rec['x'].max()), int(rec['z'].min()), int(rec['z'].max()); sx, sz = x1 - x0 + 1, z1 - z0 + 1
grid = np.full((sz, sx), np.nan); grid[rec['z'] - z0, rec['x'] - x0] = count
turned = sz > sx   # the long side goes across
W, H, DPI = 1920, 1080, 120; fig = plt.figure(figsize=(W / DPI, H / DPI), dpi=DPI); fig.patch.set_facecolor(SURF)
long_, short = (sz, sx) if turned else (sx, sz); pad = 0.045; mh = 1 - 2 * pad; mw = mh * H / W * long_ / short   # the map as tall as the frame allows
if mw > 0.86: mw = 0.86; mh = mw * W / H * short / long_
left = (1 - mw - 0.035) / 2; ax = fig.add_axes([left, (1 - mh) / 2, mw, mh]); ax.set_facecolor(SURF); ax.set_xticks([]); ax.set_yticks([])
for s in ax.spines.values(): s.set_visible(False)
vmax = float(np.nanpercentile(grid, 99.5))
if turned: im = ax.imshow(grid.T, origin='lower', extent=(z0 - 0.5, z1 + 0.5, x0 - 0.5, x1 + 0.5), cmap=BLUES, vmin=1, vmax=vmax, interpolation='nearest')   # across: z (south to the right), up: x (east)
else: im = ax.imshow(grid, extent=(x0 - 0.5, x1 + 0.5, z1 + 0.5, z0 - 0.5), cmap=BLUES, vmin=1, vmax=vmax, interpolation='nearest')
(a0, a1), (b0, b1) = ax.get_xlim(), ax.get_ylim(); da, db = a1 - a0, b1 - b0
# the corner with the fewest cavern columns takes the scale bar and the north arrow
shown = grid.T[::-1] if turned else grid; h, w = shown.shape; corners = {'lower left': shown[int(0.8 * h):, :int(0.22 * w)], 'lower right': shown[int(0.8 * h):, int(0.78 * w):], 'upper left': shown[:int(0.2 * h), :int(0.22 * w)], 'upper right': shown[:int(0.2 * h), int(0.78 * w):]}
corner = min(corners, key=lambda k: np.isfinite(corners[k]).sum()); fx = 0.035 if 'left' in corner else 0.82; fy = 0.06 if 'lower' in corner else 0.9
bar = 200 if long_ >= 600 else 100; xa = a0 + fx * da; ya = b0 + fy * db
ax.plot([xa, xa + np.sign(da) * bar], [ya, ya], color=INK, lw=3, solid_capstyle='butt'); ax.text(xa + np.sign(da) * bar / 2, ya + 0.022 * db, f'{bar} blocks', color=INK, fontsize=12, ha='center', va='bottom')
# north: -z is to the left when turned, up otherwise
nx, ny = xa + np.sign(da) * (bar + 0.07 * abs(da)), ya + 0.012 * db
if turned: ax.annotate('', xy=(nx - np.sign(da) * 0.045 * abs(da), ny), xytext=(nx, ny), arrowprops=dict(arrowstyle='-|>', color=INK, lw=1.8, mutation_scale=18)); ax.text(nx + np.sign(da) * 0.008 * abs(da), ny, 'N', color=INK, fontsize=13, ha='left', va='center', fontweight='bold')
else: ax.annotate('', xy=(nx, ny + 0.07 * db), xytext=(nx, ny - 0.01 * db), arrowprops=dict(arrowstyle='-|>', color=INK, lw=1.8, mutation_scale=18)); ax.text(nx, ny - 0.02 * db, 'N', color=INK, fontsize=13, ha='center', va='top', fontweight='bold')
if tp:
    px, py = (tp[1], tp[0]) if turned else tp
    ax.plot([px], [py], marker='+', ms=17, mew=3.4, color=SURF); ax.plot([px], [py], marker='+', ms=15, mew=1.6, color=INK)
    ax.annotate('/tp', (px, py), xytext=(9, 7), textcoords='offset points', color=INK, fontsize=12, bbox=dict(boxstyle='round,pad=0.15', fc=SURF, ec='none', alpha=0.75))
cax = fig.add_axes([left + mw + 0.012, (1 - mh) / 2 + 0.2 * mh, 0.011, 0.6 * mh]); cb = fig.colorbar(im, cax=cax); cb.outline.set_visible(False); cb.ax.tick_params(colors=MUTED, length=0, labelsize=11)
cb.set_label('cavern blocks in the column', color=INK, fontsize=12, labelpad=8)
fig.savefig(out, dpi=DPI, facecolor=SURF); print(out, f'{W} x {H}', 'turned a quarter' if turned else 'north up', f'{sx} x {sz} blocks, colour up to {vmax:.0f}')
