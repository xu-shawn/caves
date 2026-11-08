#!/usr/bin/env python3
"""A cavern's biomes by the real 26.4-snapshot-3: the cavern's blocks from icemeasure (CAVE_DUMP), the biome of every
4 x 4 x 4 cell that holds one of them from the game's own biome source (java/BiomeProbe), and the blocks per biome.
usage: verify_ice.py SEED X Y Z      prints the native line, then "by the game: ice_caves N of TOTAL; other biomes ..." """
import collections, os, struct, subprocess, sys, tempfile
import numpy as np
seed, x, y, z = sys.argv[1:5]; here = os.path.dirname(os.path.abspath(__file__))
java = os.environ.get('JAVA25', os.path.expanduser('~/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java'))
with tempfile.TemporaryDirectory() as tmp:
    dump = os.path.join(tmp, 'cavern.bin')
    native = subprocess.run([os.path.join(here, 'icemeasure'), '1'], input=f'{seed} {x} {y} {z}\n', capture_output=True, text=True, env=dict(os.environ, CAVE_DUMP=dump)).stdout.strip()
    raw = np.fromfile(dump, dtype=np.dtype([('x', '<i4'), ('z', '<i4'), ('bits', '<u8', 5)]))
cells = collections.Counter()
for col in raw:
    cx, cz = int(col['x']) >> 2, int(col['z']) >> 2
    for word in range(5):
        v = int(col['bits'][word])
        for nib in range(16):                                   # 4 blocks of one cell at a time
            n = bin((v >> (4 * nib)) & 15).count('1')
            if n: cells[(cx, (word * 64 + 4 * nib - 64) >> 2, cz)] += n
points = '\n'.join(f'{seed} {cx * 4} {cy * 4} {cz * 4}' for cx, cy, cz in cells) + '\n'
out = subprocess.run([java, '-Dcave.normal=true', '-cp', os.path.join(here, 'java', 'out') + ':' + open(os.path.join(here, 'java', 'classpath.txt')).read().strip().replace('mc/', os.path.join(here, 'mc') + '/'), 'BiomeProbe'], input=points, capture_output=True, text=True).stdout
by = collections.Counter()
for line in out.splitlines():
    w = line.split()
    if len(w) >= 6 and w[4] == 'cell': by[w[5]] += cells[(int(w[1]) >> 2, int(w[2]) >> 2, int(w[3]) >> 2)]
total = sum(cells.values())
print(native)
print(f"by the game: ice_caves {by['ice_caves']:,} of {total:,} blocks in {len(cells):,} cells" + ('' if by['ice_caves'] == total else '; other biomes: ' + ', '.join(f'{b} {n:,}' for b, n in by.most_common() if b != 'ice_caves')))
