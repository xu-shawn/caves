#!/usr/bin/env python3
"""seedcaves: the measured caverns of seed -5296147297896900941 (large biomes) and their Pareto frontiers over
(distance from the origin, size): a cavern is on a frontier if no measured cavern closer to (0, 0) is at least as large.
  total   the cavern's volume (blocks of air: underground air within 3 blocks of a connected core of 7 x 7 x 7 air cubes)
  free    its volume outside the deep dark biome (air blocks whose own 4 x 4 x 4 biome cell is not deep dark)
  strict  its volume if it has no deep dark at all (no air block in a deep dark cell); caverns with any are left out
Distance: of the cavern's /tp point (a core block near its middle) from (0, 0), horizontal.
reads measured_raw*.txt (ddmeasure lines) and verified_game.txt (measure_lb.sh lines, the real game code) if there;
writes caverns.csv (one row per cavern, largest first by distance) and prints the frontier tables.
usage: frontier.py"""
import glob, math, os, csv
here = os.path.dirname(os.path.abspath(__file__)); os.chdir(os.environ.get('WORK', here))   # WORK: the seed's directory (default: this one)
caverns = {}
for f in sorted(glob.glob('measured_raw*.txt')):
    for l in open(f):
        w = l.split()
        if len(w) < 30 or w[11] != 'at' or int(w[3]) <= 0: continue
        vol, core = int(w[3]), int(w[4]); box = tuple(int(v) for v in w[5:11]); tp = tuple(int(v) for v in w[12:15])
        i = w.index('cellblocks'); dd, zoom, cells, ddcells = int(w[i + 1]), int(w[i + 3]), int(w[i + 5]), int(w[i + 7])
        why = l.split('|')[-1].strip()
        c = caverns.setdefault((vol, box), dict(volume=vol, core=core, box=box, tp=tp, dd=dd, zoom=zoom, cells=cells, ddcells=ddcells, why=[], start=(int(w[1]), int(w[2]))))
        if why: c['why'].append(why)
game = {}   # (x0, x1, z0, z1) -> volume by the real game code
if os.path.exists('verified_game.txt'):
    for l in open('verified_game.txt'):
        w = l.split()
        if len(w) > 14 and w[11] == 'at': game[tuple(int(v) for v in w[5:11])] = int(w[3])
gamedd = {}   # tp -> (own cells, deep dark own cells by the game, cells that differ from cubiomes)
if os.path.exists('verified_biome.txt'):
    for l in open('verified_biome.txt'):
        w = l.split()
        if len(w) >= 7 and w[0] == 'cells': gamedd[(int(w[1]), int(w[2]), int(w[3]))] = (int(w[4]), int(w[5]), int(w[6]))
rows = []
for c in caverns.values():
    c['distance'] = math.hypot(c['tp'][0], c['tp'][2]); c['free'] = c['volume'] - c['dd']; c['share'] = c['dd'] / c['volume']
    c['strict'] = c['volume'] if c['dd'] == 0 and c['zoom'] == 0 else 0
    g = game.get(c['box']); c['game'] = 'same' if g == c['volume'] else ('DIFFERS %d' % g if g is not None else '')
    b = gamedd.get(c['tp']); c['gamebiome'] = '' if b is None else ('same' if b[2] == 0 else '%d of %d cells differ' % (b[2], b[0]))
    rows.append(c)
rows.sort(key=lambda c: (c['distance'], -c['volume']))
for key in ('volume', 'free', 'strict'):
    best = 0
    for c in rows:
        c['front_' + key] = c[key] > best
        if c[key] > best: best = c[key]
with open('caverns.csv', 'w', newline='') as f:
    out = csv.writer(f)
    out.writerow(['tp_x', 'tp_y', 'tp_z', 'distance', 'volume', 'deep_dark_blocks', 'free_volume', 'deep_dark_share', 'deep_dark_blocks_by_zoom', 'strict_free_volume', 'x0', 'x1', 'y0', 'y1', 'z0', 'z1',
                  'frontier_total', 'frontier_free', 'frontier_strict', 'game_volume_check', 'game_biome_check', 'picked_as'])
    for c in rows:
        out.writerow(list(c['tp']) + [round(c['distance']), c['volume'], c['dd'], c['free'], round(c['share'], 4), c['zoom'], c['strict']] + list(c['box']) +
                     [int(c['front_volume']), int(c['front_free']), int(c['front_strict']), c['game'], c['gamebiome'], ' ; '.join(c['why'])[:300]])
print(f'{len(rows)} caverns measured; largest {max(c["volume"] for c in rows):,}; largest free {max(c["free"] for c in rows):,}')
def dist(d): return f'{d / 1e6:.2f}M' if d >= 1e6 else (f'{d / 1e3:.1f}K' if d >= 1e3 else f'{d:.0f}')
for key, name in (('volume', 'TOTAL VOLUME'), ('free', 'VOLUME OUTSIDE THE DEEP DARK'), ('strict', 'CAVERNS WITH NO DEEP DARK AT ALL')):
    print(f'\nfrontier, {name}:\n  distance  /tp                              total       free   deep dark  game volume  game biomes')
    for c in rows:
        if c['front_' + key]: print(f'  {dist(c["distance"]):>8s}  {"%d %d %d" % c["tp"]:28s} {c["volume"]:>10,} {c["free"]:>10,}  {100 * c["share"]:6.1f}%    {c["game"] or "-":10s}  {c["gamebiome"] or "-"}')
