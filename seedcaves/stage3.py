#!/usr/bin/env python3
"""seedcaves stage 3b: the formula sites not measured yet that could still be on a frontier. The exact volume of a site is
at most about RATIO x its formula cluster (97 in 100 of the measured ones are under 1.72 x, the largest 2.3 x), so a site
whose RATIO x F is under the frontier at its distance cannot be on it; the same for the volume outside the deep dark with
the formula's free estimate. The rest are measured (measure_in3.txt, measured_raw3.txt).
BATCH=n: only the n most promising are measured now (largest bound over the frontier at their distance); the next round
prunes with the frontier they raise, so far fewer are measured in all than in one go.
usage: stage3.py [THREADS=6]   env RATIO=1.75 FREE_RATIO=1.9 COUNT=1 (only say how many) SUFFIX=3 (of the files written) BATCH="""
import subprocess, sys, os, math, glob, bisect
here = os.path.dirname(os.path.abspath(__file__)); os.chdir(os.environ.get('WORK', here))   # WORK: the seed's directory (default: this one)
SUFFIX = os.environ.get('SUFFIX', '3'); SEED = os.environ.get('SEED', '-5296147297896900941'); threads = sys.argv[1] if len(sys.argv) > 1 else '6'; RATIO, FREE_RATIO = float(os.environ.get('RATIO', 1.75)), float(os.environ.get('FREE_RATIO', 1.9))
meas = []
for f in glob.glob('measured_raw*.txt'):
    for l in open(f):
        w = l.split()
        if len(w) < 30 or int(w[3]) <= 0: continue
        i = w.index('cellblocks'); meas.append((math.hypot(int(w[12]), int(w[14])), int(w[3]), int(w[3]) - int(w[i + 1])))
meas.sort()
def frontier(k):
    ds, vs, best = [], [], 0
    for m in meas:
        if m[k] > best: best = m[k]; ds.append(m[0]); vs.append(best)
    return lambda d: vs[bisect.bisect_right(ds, d) - 1] if bisect.bisect_right(ds, d) > 0 else 0
front, frontFree = frontier(1), frontier(2)
done = set()
for f in glob.glob('measure_in*.txt'): done |= {tuple(l.split()[1:4]) for l in open(f) if l.strip()}
rows, margin = {}, {}
for f in glob.glob('formula_all*.txt'):
    for l in open(f):
        w = l.replace('|', ' ').split()
        F, freeM = float(w[3]), float(w[15])
        if F <= 0: continue
        for start, value, ratio, fr, why in (((w[11], w[12], w[13]), F, RATIO, front, 'F'), ((w[16], w[17], w[18]), freeM, FREE_RATIO, frontFree, 'free')):
            d = math.hypot(int(start[0]), int(start[2]))
            # the slack of 300 blocks: the cavern's /tp point is not the start, and may be a little closer
            if start not in done and value * ratio > fr(max(0, d - 300)): margin[start] = max(margin.get(start, 0), value * ratio / max(fr(max(0, d - 300)), 1.0)); rows.setdefault(start, []).append(f'{why}#0@band{max(0, math.ceil(math.log2(max(d, 1) / 1000)))}:F={F:.0f},O={float(w[4]):.0f},free={freeM:.0f}')
kept = []   # one start per 400 blocks
for s in sorted(rows, key=lambda s: -margin[s]):
    if all(abs(int(s[0]) - int(q[0])) > 400 or abs(int(s[2]) - int(q[2])) > 400 for q in kept[-4000:]): kept.append(s)
print(f'{len(rows)} starts could still be on a frontier, {len(kept)} after spacing', flush=True)
if os.environ.get('COUNT'): sys.exit()
if os.environ.get('BATCH'): kept = kept[:int(os.environ['BATCH'])]
lines = [f'{SEED} {s[0]} {s[1]} {s[2]} ' + ' '.join(rows[s]) for s in kept]
open(f'measure_in{SUFFIX}.txt', 'w').write('\n'.join(lines) + '\n')
with open(f'measured_raw{SUFFIX}.txt', 'w') as f: subprocess.run([os.path.join(here, 'ddmeasure'), threads], input='\n'.join(lines) + '\n', stdout=f, text=True)
print('measured:', sum(1 for _ in open(f'measured_raw{SUFFIX}.txt')), 'lines')
