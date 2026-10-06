#!/usr/bin/env python3
"""The cave search, a whole world per seed:
  1. cavepairs: the two repeating halves of the cave noise tabulated, the pairs of positions where both are low, where
     those pairs are in the world, the terrain there; the windows with the largest connected cluster of table points
  2. formulacave: the largest connected cavern of the cheese-cave formula on a 16-block grid around each window
  3. native/cavemeasure: the exact cavern, a flood fill over the game's terrain (the native generator, block-for-block
     the same as Minecraft 26.3) made chunk by chunk as the cavern grows (passage width 7), for the sites whose formula
     cavern could beat the best exact cavern so far
usage: search2.py NAME FIRST_SEED LAST_SEED [THREADS=11]      state and results go to runs/NAME/ (it resumes from there)
files: s1.txt (cavepairs lines), s2.txt (formulacave lines), s3.txt (CaveMeasure lines: seed x z volume core ...), events.log (BEST lines),
       seeds.txt (seed, best formula cavern, seconds), progress.txt (next seed)"""
import os, subprocess, sys, time
name, first, last = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); threads = sys.argv[4] if len(sys.argv) > 4 else '11'
here = os.path.dirname(os.path.abspath(__file__)); run = os.path.join(here, 'runs', name); os.makedirs(run, exist_ok=True)
PAIRS, TOP, BOX, MARGIN, RATIO, FLOOR, PER_SEED = 600000, 400, 1536, 48, float(os.environ.get('RATIO', '0.8')), float(os.environ.get('FLOOR', '15e6')), 3
def path(f): return os.path.join(run, f)
def append(f, lines):
    with open(path(f), 'a') as h: h.write(''.join(l + '\n' for l in lines))
def best_so_far():
    best = float(os.environ.get('BEST_START', '16928475'))
    if os.path.exists(path('s3.txt')):
        for l in open(path('s3.txt')):
            w = l.split()
            if len(w) > 4: best = max(best, float(w[3]))
    return best
def exact(sites):
    out = subprocess.run([here + '/native/cavemeasure', threads], input=''.join(sites), capture_output=True, text=True).stdout
    return [l for l in out.splitlines() if l.strip()]
seed = int(open(path('progress.txt')).read()) if os.path.exists(path('progress.txt')) else first
while seed <= last:
    t0 = time.time()
    s1 = subprocess.run([here + '/cavepairs', str(seed), str(PAIRS), str(TOP), threads], capture_output=True, text=True).stdout.splitlines()
    append('s1.txt', s1)
    s2 = subprocess.run([here + '/formulacave', threads, str(BOX), str(MARGIN)], input=''.join(l + '\n' for l in s1), capture_output=True, text=True).stdout.splitlines()
    append('s2.txt', s2)
    best = best_so_far(); cand = []
    for l in s2:
        w = l.replace('|', ' ').split()   # seed x z cluster open x0 x1 y0 y1 z0 z1 cx cy cz ...
        if float(w[3]) >= max(FLOOR, RATIO * best): cand.append((float(w[3]), f'{w[0]} {w[11]} {w[12]} {w[13]} formula {w[3]}\n'))
    cand.sort(reverse=True)
    top_formula = max((float(l.replace('|', ' ').split()[3]) for l in s2), default=0)
    if cand:
        results = exact([c[1] for c in cand[:PER_SEED]])
        append('s3.txt', results)
        for r in results:
            w = r.split()
            if float(w[3]) > best:
                best = float(w[3])
                append('events.log', [f'BEST {int(best)} seed={w[0]} x={w[5]}..{w[6]} y={w[7]}..{w[8]} z={w[9]}..{w[10]} tp={w[12]},{w[13]},{w[14]} {time.strftime("%FT%TZ", time.gmtime())}'])
    append('seeds.txt', [f'{seed} {top_formula:.0f} {len(cand)} {time.time() - t0:.1f}'])
    seed += 1
    open(path('progress.txt'), 'w').write(str(seed))
