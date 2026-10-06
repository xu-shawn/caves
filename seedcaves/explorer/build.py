#!/usr/bin/env python3
"""seedcaves/explorer: builds index.html, the one-file explorer of the seed's measured caverns, from
  ../caverns.csv            every measured cavern (frontier flags, game-code checks)
  map_list.txt              the caverns that get their own map ("tp_x tp_y tp_z" a line)
  ../export/, cells/        their biome-cell columns (ddmeasure with DD_EXPORT_DIR: 48 bytes a column of 4 x 4 x 4 cells:
                            int32 qx, qz; uint64 own[2] (cells with air of the cavern, bit qy + 16); uint64 near[2];
                            int32 qtop (highest deep dark cell, INT_MIN: none); int32 0)
  template.html             the page; /*DATA*/ is replaced by the JSON
A cavern's map is two small pictures, each a byte per pixel of counts followed by a byte per pixel of deep dark counts,
deflate-compressed and in base64: the top view (a pixel per column of cells: the cells of the column with air of the
cavern, and the deep dark ones among them) and the side view looking north (a pixel per x and height: the cells along z
with air, and the deep dark ones). The page unpacks them itself (DecompressionStream). They were PNGs read back from a
canvas until that turned out to give made-up pixels in browsers that resist fingerprinting. The frontier caverns at 4
blocks a pixel, the others at 8.
usage: build.py"""
import base64, csv, json, os, zlib, numpy as np
here = os.path.dirname(os.path.abspath(__file__)); os.chdir(here); SEED = '-5296147297896900941'
rows = list(csv.DictReader(open('../caverns.csv')))
rows.sort(key=lambda r: float(r['distance']))
wanted = {tuple(l.split()) for l in open('map_list.txt') if l.strip()}
record = np.dtype([('qx', '<i4'), ('qz', '<i4'), ('own', '<u8', 2), ('near', '<u8', 2), ('qtop', '<i4'), ('pad', '<i4')])
def packed(red, green):   # (counts over 255 are scaled down, rounded up so that a cell with air stays a cell with air)
    by = max(1, -(-int(red.max()) // 255)); red, green = -(-red // by), -(-green // by)
    planes = np.minimum(red, 255).astype(np.uint8).tobytes() + np.minimum(green, 255).astype(np.uint8).tobytes()
    return base64.b64encode(zlib.compress(planes, 9)).decode()
def cavern_map(r, fine):
    key = (r['tp_x'], r['tp_y'], r['tp_z']); path = next((p for p in (f'../export/cells_{SEED}_{key[0]}_{key[1]}_{key[2]}.bin', f'cells/cells_{SEED}_{key[0]}_{key[1]}_{key[2]}.bin') if os.path.exists(p)), None)
    if path is None: return None
    cells = np.fromfile(path, record); own = cells['own']; cells = cells[(own[:, 0] != 0) | (own[:, 1] != 0)]
    if len(cells) == 0: return None
    # bits[column, k]: cell qy = k - 16 of the column holds air of the cavern (k < 80: y -64 .. 255)
    bits = np.zeros((len(cells), 80), bool)
    for k in range(80): bits[:, k] = (cells['own'][:, k // 64] >> np.uint64(k % 64)) & np.uint64(1)
    deep = bits & (np.arange(80)[None, :] - 16 <= cells['qtop'][:, None])
    step = 1 if fine else 2   # cells a pixel
    qx0, qz0 = int(cells['qx'].min()) // step * step, int(cells['qz'].min()) // step * step
    px, pz = (cells['qx'] - qx0) // step, (cells['qz'] - qz0) // step; w, h = int(px.max()) + 1, int(pz.max()) + 1
    top = np.zeros((h, w), np.int32); topDeep = np.zeros((h, w), np.int32)
    np.add.at(top, (pz, px), bits.sum(1)); np.add.at(topDeep, (pz, px), deep.sum(1))
    levels = np.nonzero(bits.any(0))[0]; k0, k1 = int(levels.min()), int(levels.max())   # the heights that have air
    side = np.zeros((k1 - k0 + 1, w), np.int32); sideDeep = np.zeros_like(side)
    for k in range(k0, k1 + 1):
        np.add.at(side[k1 - k], px[bits[:, k]], 1); np.add.at(sideDeep[k1 - k], px[deep[:, k]], 1)
    return dict(res=4 * step, x0=qx0 * 4, z0=qz0 * 4, w=w, h=h, top=packed(top, topDeep), tmax=int(-(-top.max() // max(1, -(-int(top.max()) // 255)))),
                y0=-64 + 4 * k0, y1=-64 + 4 * k1 + 3, sh=k1 - k0 + 1, side=packed(side, sideDeep), smax=int(-(-side.max() // max(1, -(-int(side.max()) // 255)))))
cav, maps, missing = [], [], 0
for r in rows:
    flags = int(r['frontier_total']) + 2 * int(r['frontier_free']) + 4 * int(r['frontier_strict']); key = (r['tp_x'], r['tp_y'], r['tp_z']); index = -1
    if key in wanted:
        m = cavern_map(r, fine=flags != 0)
        if m: index = len(maps); maps.append(m)
        else: missing += 1
    differ, own = -1, 0   # the game's biome check: cells that differ from the count used, of the cells with air
    g = r['game_biome_check']
    if g == 'same': differ = 0
    elif g: differ, own = int(g.split()[0]), int(g.split()[2])
    cav.append([int(r['tp_x']), int(r['tp_y']), int(r['tp_z']), int(r['volume']), int(r['deep_dark_blocks']), int(r['deep_dark_blocks_by_zoom'])] + [int(r[k]) for k in ('x0', 'x1', 'y0', 'y1', 'z0', 'z1')] +
               [flags, 1 if r['game_volume_check'] == 'same' else 0, differ, own, index])
data = dict(seed=SEED, cav=cav, maps=maps)
text = json.dumps(data, separators=(',', ':'))
page = open('template.html').read(); assert '/*DATA*/' in page
open('index.html', 'w').write(page.replace('/*DATA*/', text))
sizes = sorted(len(m['top']) + len(m['side']) for m in maps)
print(f'{len(cav)} caverns, {len(maps)} with a map ({missing} of the list without cells yet); maps {sum(sizes) / 1e6:.2f} MB (median {sizes[len(sizes) // 2] / 1e3:.1f} KB, largest {sizes[-1] / 1e3:.1f} KB); page {os.path.getsize("index.html") / 1e6:.2f} MB')
