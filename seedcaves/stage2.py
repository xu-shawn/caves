#!/usr/bin/env python3
"""seedcaves stage 2 and 3 for seed -5296147297896900941 (large biomes).
  2. the formula stage (formulacave_lb: 16-block grid, 1536-block box, clusters under the large-biomes surface, deep dark
     estimate) on every ring's scanner sites (ring_*.txt) and on a lattice of sites over the innermost blocks
     (LATTICE_REACH, every 512 blocks: there the whole area is covered directly, not by the scanner's coarse windows)
  3. the exact measure with the deep dark share (ddmeasure, large biomes) of the best sites of every band of distances
     (bands a factor 2 wide, by the distance of the start point): the K_TOTAL best by formula cluster F, the K_TOTAL best
     by F + 2 O (O: all open samples of the box), the K_FREE best by the most blocks outside the deep dark in one cluster
     and the K_FREE best by the largest cluster's blocks outside it; starts within 400 blocks of a better one are skipped.
writes formula_all.txt (every formula line), measure_in.txt, measured_raw.txt (with SUFFIX in their names)
usage: stage2.py [THREADS=6]      env K_TOTAL=8 K_FREE=8 LATTICE_REACH=8704 SUFFIX="""
import subprocess, sys, os, math, glob
here = os.path.dirname(os.path.abspath(__file__)); os.chdir(os.environ.get('WORK', here))   # WORK: the seed's directory (default: this one)
SEED = os.environ.get('SEED', '-5296147297896900941'); threads = sys.argv[1] if len(sys.argv) > 1 else '6'
SUFFIX = os.environ.get('SUFFIX', '')   # a later, deeper run: its own files, and the starts measured before are not measured again
K_TOTAL, K_FREE, REACH = int(os.environ.get('K_TOTAL', 8)), int(os.environ.get('K_FREE', 8)), int(os.environ.get('LATTICE_REACH', 8704))
sites = []
seen = set()
for f in sorted(glob.glob('ring*_*.txt')):
    for l in open(f):
        w = l.split()
        if len(w) > 3 and (w[1], w[2]) not in seen: seen.add((w[1], w[2])); sites.append(l.strip() + f' src {f[:-4]}')
sites += [f'{SEED} {x} {z} 0 0 0 0 0 src lattice' for x in range(-REACH, REACH + 1, 512) for z in range(-REACH, REACH + 1, 512)]
out = subprocess.run([os.path.join(here, 'formulacave_lb'), threads, '1536', '48'], input='\n'.join(sites) + '\n', capture_output=True, text=True).stdout.splitlines()
open(f'formula_all{SUFFIX}.txt', 'w').write('\n'.join(out) + '\n')
rows = []
for l in out:
    w = l.replace('|', ' ').split()   # seed x z F O x0 x1 y0 y1 z0 z1 cx cy cz freeOfLargest mostFree fx fy fz ...
    F, O, freeL, freeM = float(w[3]), float(w[4]), float(w[14]), float(w[15])
    if F <= 0: continue
    rows.append(dict(F=F, O=O, freeL=freeL, freeM=freeM, start=(int(w[11]), int(w[12]), int(w[13])), fstart=(int(w[16]), int(w[17]), int(w[18])), src=w[-1]))
def band(p): d = math.hypot(p[0], p[2]); return max(0, math.ceil(math.log2(max(d, 1) / 1000)))   # band b: distances up to 1000 x 2^b
picked = {}   # start point -> why
def pick(key, start_of, k, why):
    for b in sorted({band(start_of(r)) for r in rows}):
        kept = []
        for r in sorted((r for r in rows if band(start_of(r)) == b), key=key, reverse=True):
            s = start_of(r)
            if key(r) <= 0 or len(kept) >= k: break
            if any(abs(s[0] - q[0]) <= 400 and abs(s[2] - q[2]) <= 400 for q in kept): continue
            kept.append(s); picked.setdefault(s, []).append(f'{why}#{len(kept)}@band{b}:F={r["F"]:.0f},O={r["O"]:.0f},free={r["freeM"]:.0f}')
pick(lambda r: r['F'], lambda r: r['start'], K_TOTAL, 'F')
pick(lambda r: r['F'] + 2 * r['O'], lambda r: r['start'], K_TOTAL, 'FO')
pick(lambda r: r['freeM'], lambda r: r['fstart'], K_FREE, 'free')
pick(lambda r: r['freeL'], lambda r: r['start'], K_FREE, 'freeL')
done = set()
for f in glob.glob('measure_in*.txt'):
    if f != f'measure_in{SUFFIX}.txt': done |= {tuple(l.split()[1:4]) for l in open(f) if l.strip()}
lines = [f'{SEED} {s[0]} {s[1]} {s[2]} ' + ' '.join(why) for s, why in picked.items() if (str(s[0]), str(s[1]), str(s[2])) not in done]
open(f'measure_in{SUFFIX}.txt', 'w').write('\n'.join(lines) + '\n')
print(f'{len(sites)} sites, {len(rows)} with a cluster, {len(lines)} starts to measure', flush=True)
with open(f'measured_raw{SUFFIX}.txt', 'w') as f: subprocess.run([os.path.join(here, 'ddmeasure'), threads], input='\n'.join(lines) + '\n', stdout=f, text=True)
print(f'measured: {sum(1 for _ in open(f"measured_raw{SUFFIX}.txt"))} lines')
