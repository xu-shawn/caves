#!/usr/bin/env python3
"""Pictures of the record cavern (55,830,955 blocks, seed 6008414183811625950) from its own blocks.
Input: record/cavern.bin, written by `echo "SEED X Y Z" | CAVE_DUMP=record/cavern.bin native/cavemeasure 1` (a record
per column: x, z, then the column's 320 bits from y -64 up), and cave_55.8M.npz (caveview3.py's two vertical cuts of
the whole terrain through the /tp point).
Output: record/map.png (open height, floor and roof of the cavern per column), record/sections.png (two cuts with the
cavern picked out, and its area by height), record/render3d.png (the cavern as a solid, seen from above the south-east),
record/volume4.bin (the cavern on 4-block cells, a bit per cell, for the 3D viewer) and record/facts.json.
usage: record/render.py"""
import os, json, numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap, ListedColormap
here = os.path.dirname(os.path.abspath(__file__)); SEED = 6008414183811625950; TP = (19079170, 13, -7883600)
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'
BLUES = LinearSegmentedColormap.from_list('blues', ['#e6eefa', '#9dbfee', '#2a78d6', '#0c2f66'])       # one hue, light to dark: magnitudes
EARTH = LinearSegmentedColormap.from_list('earth', ['#3b1f0e', '#8a4a1c', '#d98a3c', '#f6d9a8', '#fffaf0'])   # one hue, dark (low) to light (high): heights
rec = np.fromfile(os.path.join(here, 'cavern.bin'), dtype=np.dtype([('x', '<i4'), ('z', '<i4'), ('w', '<u8', 5)]))
x0, x1, z0, z1 = rec['x'].min(), rec['x'].max(), rec['z'].min(), rec['z'].max(); sx, sz = x1 - x0 + 1, z1 - z0 + 1
bits = np.unpackbits(rec['w'].view(np.uint8).reshape(len(rec), 40), axis=1, bitorder='little')   # [column, y + 64]
count = bits.sum(1).astype(np.int32); ys = np.arange(320) - 64
floor_y = ys[bits.argmax(1)]; roof_y = ys[319 - bits[:, ::-1].argmax(1)]; volume = int(count.sum()); y0, y1 = int(floor_y.min()), int(roof_y.max())
print(f'{len(rec):,} columns, {volume:,} blocks, x {x0}..{x1}, y {y0}..{y1}, z {z0}..{z1}')
ix, iz = rec['x'] - x0, rec['z'] - z0
def grid(values, fill=np.nan): g = np.full((sz, sx), fill, dtype=float); g[iz, ix] = values; return g
thick, lo, hi = grid(count), grid(floor_y), grid(roof_y)
area_by_y = bits.sum(0).astype(np.int64)   # open columns at each height
facts = {'seed': str(SEED), 'tp': TP, 'volume': volume, 'columns': int(len(rec)), 'box': [int(x0), int(x1), y0, y1, int(z0), int(z1)], 'size': [int(sx), y1 - y0 + 1, int(sz)], 'mean_open_height': volume / len(rec),
         'largest_open_height': int(count.max()), 'share_of_box_footprint': len(rec) / (sx * sz), 'widest_level': int(ys[area_by_y.argmax()]), 'columns_at_widest_level': int(area_by_y.max())}

def style(ax):
    ax.set_facecolor(SURF); ax.tick_params(colors=MUTED, length=0, labelsize=8)
    for s in ax.spines.values(): s.set_color(GRID)
def blocks(v, pos=None): return f'{v:,.0f}'
def scalebar(ax, length=500):
    xa, za = x0 + 0.04 * sx, z1 - 0.045 * sz; ax.plot([xa, xa + length], [za, za], color=INK, lw=2.5, solid_capstyle='butt'); ax.text(xa + length / 2, za - 0.012 * sz, f'{length} blocks', color=INK, fontsize=8, ha='center', va='bottom')

# 1. the map: open height per column, and the floor and the roof
fig = plt.figure(figsize=(15.5, 7.6), dpi=140); fig.patch.set_facecolor(SURF); gs = fig.add_gridspec(2, 3, width_ratios=[2.05, 1, 0.04], wspace=0.16, hspace=0.2)
ax = fig.add_subplot(gs[:, 0]); style(ax)
im = ax.imshow(thick, extent=(x0 - 0.5, x1 + 0.5, z1 + 0.5, z0 - 0.5), cmap=BLUES, vmin=1, vmax=np.nanpercentile(thick, 99.5), interpolation='nearest')
ax.plot([TP[0]], [TP[2]], marker='+', ms=14, mew=2.2, color=SURF); ax.plot([TP[0]], [TP[2]], marker='+', ms=12, mew=1.2, color=INK); ax.annotate('/tp', (TP[0], TP[2]), xytext=(8, -4), textcoords='offset points', color=INK, fontsize=9)
scalebar(ax); ax.set_xlabel('x (blocks)', color=INK2, fontsize=9); ax.set_ylabel('z (blocks): south is down', color=INK2, fontsize=9); ax.xaxis.set_major_formatter(blocks); ax.yaxis.set_major_formatter(blocks); ax.xaxis.set_major_locator(plt.MaxNLocator(5)); ax.yaxis.set_major_locator(plt.MaxNLocator(6))
ax.set_title(f'The record cavern from above: {volume:,} blocks', loc='left', fontsize=13, color=INK, fontweight='bold')
cb = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.015); cb.set_label('open height of the cavern in the column, blocks', color=INK2, fontsize=9); cb.ax.tick_params(colors=MUTED, length=0, labelsize=8); cb.outline.set_edgecolor(GRID)
for row, (data, name) in enumerate(((hi, 'its roof: height of the highest block'), (lo, 'its floor: height of the lowest block'))):
    a = fig.add_subplot(gs[row, 1]); style(a); m = a.imshow(data, extent=(x0 - 0.5, x1 + 0.5, z1 + 0.5, z0 - 0.5), cmap=EARTH, vmin=-54, vmax=184, interpolation='nearest'); a.set_xticks([]); a.set_yticks([])
    a.set_title(name, loc='left', fontsize=10, color=INK2); c = fig.colorbar(m, cax=fig.add_subplot(gs[row, 2])); c.set_label('y', color=INK2, fontsize=9); c.ax.tick_params(colors=MUTED, length=0, labelsize=8); c.outline.set_edgecolor(GRID)
fig.text(0.125, 0.035, f'seed {SEED}   /tp {TP[0]} {TP[1]} {TP[2]}   {sx:,} × {sz:,} blocks across, y {y0} to {y1}; air under {len(rec):,} columns ({100 * len(rec) / (sx * sz):.0f}% of that rectangle), {volume / len(rec):.0f} blocks of open height on average',
         color=INK2, fontsize=9)
fig.savefig(os.path.join(here, 'map.png'), facecolor=SURF, bbox_inches='tight'); plt.close(fig); print('wrote map.png')

# 2. two cuts through the /tp point with the cavern picked out, and the cavern's area by height
cuts = np.load(os.path.join(here, '..', 'cave_55.8M.npz')); CX0, CZ0 = 19078112, -7884800   # the origin caveview3.py was given
CLASS = ListedColormap(['#6f6c66', '#e9dcc0', '#3f78c9', '#e2622b', '#dce9f6', '#f2a23a'])   # rock, other cave air, water, lava, open sky, the cavern
def cut_with_cavern(classes, along, fixed_axis, fixed_value, origin):
    c = classes.astype(np.uint8).copy(); sel = rec[fixed_axis] == fixed_value; pos = rec[along][sel] - origin; ok = (pos >= 0) & (pos < len(c)); b = bits[sel][ok]
    rows, heights = np.nonzero(b); c[pos[ok][rows], heights] = 5; return c
fig, axes = plt.subplots(3, 1, figsize=(13.5, 10.2), dpi=140, gridspec_kw={'height_ratios': [1, 1, 0.8], 'hspace': 0.42}); fig.patch.set_facecolor(SURF)
for a, (classes, along, fixed_axis, fixed_value, origin, label) in zip(axes[:2], ((cuts['cutx'], 'x', 'z', TP[2], CX0, f'west to east along z = {TP[2]:,}'), (cuts['cutz'], 'z', 'x', TP[0], CZ0, f'north to south along x = {TP[0]:,}'))):
    style(a); c = cut_with_cavern(classes, along, fixed_axis, fixed_value, origin)
    a.imshow(c.T[:300], origin='lower', extent=(origin - 0.5, origin + len(c) - 0.5, -64, 236), cmap=CLASS, vmin=0, vmax=5, aspect='auto', interpolation='nearest')
    a.plot([TP[0] if along == 'x' else TP[2]], [TP[1]], marker='+', ms=13, mew=1.6, color=INK); a.set_ylim(-64, 236); a.set_ylabel('y', color=INK2, fontsize=9); a.xaxis.set_major_formatter(blocks)
    a.set_title(f'Cut {label}', loc='left', fontsize=11, color=INK, fontweight='bold'); a.set_xlabel(f'{along} (blocks)', color=INK2, fontsize=9)
handles = [plt.Rectangle((0, 0), 1, 1, color=CLASS(i / 5)) for i in (5, 1, 0, 2, 3, 4)]
leg = axes[0].legend(handles, ['the cavern', 'other cave air', 'rock', 'water', 'lava', 'open sky'], loc='upper left', ncol=6, frameon=True, fontsize=8.5, facecolor=SURF, edgecolor=GRID, framealpha=0.9); [t.set_color(INK2) for t in leg.get_texts()]
a = axes[2]; style(a); a.grid(True, color=GRID, lw=0.8); a.set_axisbelow(True); m = (ys >= y0 - 2) & (ys <= y1 + 2)
a.fill_between(ys[m], area_by_y[m] / 1e3, color='#2a78d6', alpha=0.18, lw=0); a.plot(ys[m], area_by_y[m] / 1e3, color='#2a78d6', lw=2)
half = ys[np.searchsorted(np.cumsum(area_by_y), volume / 2)]; a.axvline(half, color=INK2, lw=1, ls=(0, (3, 3))); a.text(half + 28, area_by_y.max() / 1e3 * 0.93, f'dashed: half of the volume is below y {half}', color=INK2, fontsize=9)
a.set_xlabel('height y', color=INK2, fontsize=9); a.set_ylabel('open columns at that height, thousands', color=INK2, fontsize=9); a.set_xlim(y0 - 2, y1 + 2); a.set_ylim(0)
a.set_title('The cavern level by level', loc='left', fontsize=11, color=INK, fontweight='bold')
for s in ('top', 'right'): a.spines[s].set_visible(False)
fig.savefig(os.path.join(here, 'sections.png'), facecolor=SURF, bbox_inches='tight'); plt.close(fig); print('wrote sections.png')
facts['half_volume_below_y'] = int(half)

# 3. the cavern on coarser cells: 2 blocks for the picture, 4 for the viewer
def cells(step):
    nx, nz, ny = -(-sx // step), -(-sz // step), 320 // step; v = np.zeros((nz, nx, ny), dtype=np.uint16)
    np.add.at(v, (iz // step, ix // step), bits.reshape(len(rec), ny, step).sum(2).astype(np.uint16)); return v
v2 = cells(2) >= 4                                    # a 2-block cell counts when half of its 8 blocks are the cavern's
ya, yb = (y0 + 64) // 2, (y1 + 64) // 2 + 1; v2 = v2[:, :, ya:yb]; nz, nx, ny = v2.shape
# Seen from above the south-east: a cell at (x, z, y) lands on the picture at column x - z and row (x + z) / 2 - 2 y, and
# the levels are painted from the lowest up, so what is higher covers what is behind and below it. A cell is lit on
# top if nothing is above it, and its colour is its height.
W, H, UP = nx + nz + 2, (nx + nz) // 2 + 2 * ny + 4, 2; BACK = np.array(matplotlib.colors.to_rgb('#14110f'), dtype=np.float32); img = np.zeros((H, W, 3), dtype=np.float32); img[:] = BACK; zz, xx = np.nonzero(np.ones((nz, nx), bool))
pad = np.zeros((nz, nx, 1), bool); above = np.concatenate([v2[:, :, 1:], pad], 2); east = np.zeros_like(v2); east[:, :-1] = v2[:, 1:]; south = np.zeros_like(v2); south[:-1] = v2[1:]
for k in range(ny):
    layer = v2[:, :, k]
    if not layer.any(): continue
    z_, x_ = np.nonzero(layer); u = x_ - z_ + nz; r = (x_ + z_) // 2 + UP * (ny - 1 - k) + 2
    top = ~above[z_, x_, k]; shade = np.where(top, 1.0, np.where(~east[z_, x_, k], 0.78, np.where(~south[z_, x_, k], 0.6, 0.5)))
    colour = np.array(EARTH(0.08 + 0.92 * k / (ny - 1)))[:3] * shade[:, None]
    for du in (0, 1):
        for dr in range(UP + 1): img[r + dr, u + du] = colour      # a cell is two pixels wide and UP + 1 tall (its side shows below its top)
drawn = np.nonzero((img != BACK).any(2)); r0, r1, c0, c1 = max(0, drawn[0].min() - 40), min(H, drawn[0].max() + 40), max(0, drawn[1].min() - 40), min(W, drawn[1].max() + 40); img = img[r0:r1, c0:c1]; H, W = img.shape[:2]   # the picture cut to what is drawn
fig = plt.figure(figsize=(15, 15 * H / W + 0.9), dpi=140); fig.patch.set_facecolor('#14110f'); a = fig.add_axes([0, 0, 1, H / W * 15 / (15 * H / W + 0.9)]); a.imshow(img, interpolation='nearest'); a.axis('off')
fig.text(0.02, 0.975, f'The record cavern as a solid: {volume:,} blocks, seen from above the south-east', color='#f6efe2', fontsize=14, fontweight='bold', va='top')
fig.text(0.02, 0.945, f'seed {SEED}, /tp {TP[0]} {TP[1]} {TP[2]}. Air is drawn, rock is not. Colour is height (dark y {y0}, light y {y1}); heights are drawn twice their size. {sx:,} × {sz:,} blocks.', color='#cdbfa8', fontsize=9.5, va='top')
fig.savefig(os.path.join(here, 'render3d.png'), facecolor='#14110f'); plt.close(fig); print('wrote render3d.png', img.shape)

v4 = cells(4) >= 24; ya, yb = (y0 + 64) // 4, (y1 + 64) // 4 + 1; v4 = v4[:, :, ya:yb]                 # [z, x, y]: a 4-block cell counts when 24 of its 64 blocks are the cavern's
np.packbits(v4.transpose(2, 0, 1).ravel()).tofile(os.path.join(here, 'volume4.bin'))                    # order y, z, x; a bit per cell
facts['viewer'] = {'cell': 4, 'shape_yzx': [int(v4.shape[2]), int(v4.shape[0]), int(v4.shape[1])], 'origin': [int(x0), int(ya * 4 - 64), int(z0)], 'cells': int(v4.sum())}
json.dump(facts, open(os.path.join(here, 'facts.json'), 'w'), indent=1); print(json.dumps(facts))
