#!/usr/bin/env python3
"""icecaves/results.py: RESULTS.md from every run in runs/ (a cavern once), with the checks listed in checked.txt.
checked.txt: "SEED VOLUME terrain|biome|both [note]" per cavern that was checked in the snapshot itself.
usage: results.py [--top 15] > RESULTS.md"""
import os, sys

here = os.path.dirname(os.path.abspath(__file__)); top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 15
checked = {}
if os.path.exists(f'{here}/checked.txt'):
    for l in open(f'{here}/checked.txt'):
        f = l.split(None, 3)
        if len(f) >= 3: checked[(f[0], int(f[1]))] = (f[2], f[3].strip() if len(f) > 3 else '')
full, near, ice = {}, {}, {}; seeds = set(); seconds = 0.0
for run in sorted(os.listdir(f'{here}/runs')):
    log, res = f'{here}/runs/{run}/seeds.log', f'{here}/runs/{run}/results.txt'
    if not (os.path.exists(log) and os.path.exists(res)): continue
    for l in open(log):
        f = l.split()
        if len(f) >= 7 and 'failed' not in l: seeds.add(f[0]); seconds += float(f[2]) + float(f[4]) + float(f[6])
    for l in open(res):
        f = l.split()
        if 'seconds' not in f: continue
        i = f.index('seconds'); volume = int(f[3]); inside = int(f[i + 3]); tp = ' '.join(f[i - 5:i - 2])
        if 'left' in f[i + 4:i + 6] or volume == 0: continue
        if inside == volume: full[(f[0], volume)] = tp
        elif inside >= 0.999 * volume: near[(f[0], volume)] = (tp, inside)
        if inside > ice.get(f[0], (0,))[0]: ice[f[0]] = (inside, volume, tp)

def mark(seed, volume):
    c = checked.get((seed, volume))
    return '' if not c else {'both': 'terrain and biome', 'terrain': 'terrain', 'biome': 'biome'}[c[0]] + (f' ({c[1]})' if c[1] else '')

print('# Ice caves caverns found (Minecraft 26.4-snapshot-3)\n')
print(f'{len(seeds):,} seeds scanned in {seconds / 3600:.0f} thread-hours (a 12-thread laptop and, for the runs named box, a rented 32-thread machine). Volumes are from the native measure '
      '(`icemeasure`). "Checked" is what was confirmed with the snapshot itself: *terrain* = the same volume on the terrain the '
      'snapshot\'s code generates (`measure_snapshot.sh`), *biome* = the snapshot\'s biome source asked for every cell of the cavern '
      '(`verify_ice.py`).\n')
print('## Ice caves throughout\n\nEvery block of the cavern is in an ice caves cell.\n')
print('| blocks | seed | /tp | checked |\n|---:|---|---|---|')
for (seed, volume), tp in sorted(full.items(), key=lambda kv: -kv[0][1])[:top]: print(f'| {volume:,} | {seed} | {tp} | {mark(seed, volume)} |')
sizes = sorted((k[1] for k in full), reverse=True)
print('\n' + ', '.join(f'{sum(v >= t for v in sizes)} of {t / 1e6:g}M+' for t in (2e6, 2.5e6, 3e6, 3.5e6, 4e6)) + ' in all.\n')
print('## All but a few blocks\n\nThe core is ice caves throughout; the blocks outside are in the 3-block rim.\n')
print('| blocks | outside ice caves | seed | /tp | checked |\n|---:|---:|---|---|---|')
for (seed, volume), (tp, inside) in sorted(near.items(), key=lambda kv: -kv[0][1])[:max(5, top // 3)]: print(f'| {volume:,} | {volume - inside:,} | {seed} | {tp} | {mark(seed, volume)} |')
print('\n## Most ice caves in one cavern\n\nThe cavern\'s blocks in ice caves cells; the rest of it is under other biomes.\n')
print('| ice caves blocks | cavern | share | seed | /tp | checked |\n|---:|---:|---:|---|---|---|')
for seed, (inside, volume, tp) in sorted(ice.items(), key=lambda kv: -kv[1][0])[:max(8, top // 2)]:
    if inside == volume: continue
    print(f'| {inside:,} | {volume:,} | {100 * inside / volume:.0f}% | {seed} | {tp} | {mark(seed, volume)} |')
