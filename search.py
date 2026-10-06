#!/usr/bin/env python3
"""The cave search, three stages per seed, over the 65,536 x 65,536 blocks around the origin:
  1. proxyscan3: the cheese-cave formula under the estimated surface, dry points only, in 512-block windows (C++)
  2. CaveAir: the real game's terrain for one chunk in 16 of the window: how much underground air is really there
  3. exact.py: the real terrain of the whole region and the largest cavern in it (cavemetric, passage width 7)
Stage 3 only runs for sites whose stage-2 air could beat the best cavern so far.
usage: search.py NAME FIRST_SEED LAST_SEED [THREADS=11]      state and results go to runs/NAME/ (it resumes from there)
files: s1.txt (seed x z score open surface), s2.txt (seed x z air water top | score open surface), s3.txt (exact.py lines),
       events.log (BEST lines), progress.txt (next seed)"""
import os, subprocess, sys, time
name, first, last = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); threads = sys.argv[4] if len(sys.argv) > 4 else '11'
here = os.path.dirname(os.path.abspath(__file__)); run = os.path.join(here, 'runs', name); os.makedirs(run, exist_ok=True)
T1, T2, BATCH, AREA, TILE = 24, 2700, 40, 32768, 32768
java = os.environ.get('JAVA25', os.path.expanduser('~/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java'))
cp = here + '/java/out:' + open(here + '/java/classpath.txt').read().strip().replace('mc/', here + '/mc/')
def path(f): return os.path.join(run, f)
def best_so_far():
    best = float(os.environ.get('BEST_START', '4750000'))
    if os.path.exists(path('s3.txt')):
        for l in open(path('s3.txt')):
            w = l.split()
            if len(w) > 11 and w[11] == 'ok': best = max(best, float(w[3]))
    return best
def exact(sites, size):
    out = subprocess.run([here + '/exact.py', str(size), '3', threads], input=''.join(sites), capture_output=True, text=True, env={**os.environ, 'CAVE_TMP': run}).stdout
    return [l for l in out.splitlines() if l.strip()]
seed = int(open(path('progress.txt')).read()) if os.path.exists(path('progress.txt')) else first
pending = []
while seed <= last:
    t0 = time.time(); found = []
    for z0 in range(-AREA, AREA, TILE):
        for x0 in range(-AREA, AREA, TILE):
            out = subprocess.run([here + '/proxyscan3', str(seed), str(x0), str(z0), str(TILE), str(T1), str(T2), threads], capture_output=True, text=True).stdout
            found += [l for l in out.splitlines() if l.strip()]
    with open(path('s1.txt'), 'a') as f: f.write(''.join(l + '\n' for l in found))
    pending += found; seed += 1
    if (seed - first) % BATCH == 0 or seed > last:
        if pending:
            lines = ''.join(f'{w[0]} {w[1]} {w[2]} 512 {w[3]} {w[4]} {w[5]}\n' for w in (l.split() for l in pending))
            air = subprocess.run([java, '-Xmx3g', '-cp', cp, 'CaveAir', threads, '4'], input=lines, capture_output=True, text=True).stdout.splitlines()
            with open(path('s2.txt'), 'a') as f: f.write(''.join(l + '\n' for l in air))
            best = best_so_far()
            good = [l + '\n' for l in air if len(l.split()) > 3 and float(l.split()[3]) >= best / 0.8]
            if good:
                results = exact(good, 1024)
                for i, r in enumerate(results):
                    w = r.split()
                    if w[11] == 'edge':   # the cavern runs off the dump: again, bigger and centred on it
                        cx, cz = (int(w[5]) + int(w[6])) // 2, (int(w[9]) + int(w[10])) // 2
                        again = exact([f'{w[0]} {cx} {cz} recentred {" ".join(w[13:])}\n'], 1536)
                        if again: results[i] = again[0]
                with open(path('s3.txt'), 'a') as f: f.write(''.join(r + '\n' for r in results))
                for r in results:
                    w = r.split()
                    if float(w[3]) > best and w[11] == 'ok':
                        best = float(w[3])
                        with open(path('events.log'), 'a') as f: f.write(f'BEST {int(best)} seed={w[0]} x={w[5]}..{w[6]} y={w[7]}..{w[8]} z={w[9]}..{w[10]} {time.strftime("%FT%TZ", time.gmtime())}\n')
            pending = []
        open(path('progress.txt'), 'w').write(str(seed))
        with open(path('log.txt'), 'a') as f: f.write(f'{time.strftime("%FT%TZ", time.gmtime())} next seed {seed}, best {best_so_far():.0f}\n')
