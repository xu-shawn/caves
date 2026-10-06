#!/usr/bin/env python3
"""The cave search behind the seed gate. seedgate keeps appending "seed gate lowest" lines to a file; this takes the
seeds with the lowest gate values that nobody has claimed yet and runs the three stages on each:
  1. cavepairs3   the pairs of low spots of the two halves of the cave noise, their places in the world, the terrain
                  there, and for the best places the cluster on tables of half the step
  2. formulacave  the largest connected cavern of the cheese-cave formula around each of the best 400 windows
  3. native/cavemeasure   the exact cavern (flood fill over the native game terrain) of every formula cavern of at
                          least FLOOR blocks (and RATIO x the best exact one so far), up to PER_SEED per seed; and of
                          smaller ones in a window with a lot of open volume (the formula's largest cluster F and all
                          its open samples O: F >= F_MID with O >= O_MID, or F >= F_LOW with O >= O_HIGH), because the
                          giants are often several formula clusters that join in the real terrain; FO_MIN: and F + O at least this
usage: [SCANNER=cavepairs3] [PAIRS=600000] [PLACES=15000] [MIN_CUT=0] [TOP=400] [MIN_SCORE=0] [FLOOR=17e6] [PER_SEED=40] [RATIO=0] [F_MID O_MID F_LOW O_HIGH] [FO_MIN=0] [O_WEIGHT=1] [CUT_FREE] [TERRAIN_DIV=92] [BEST_START=..] search3.py NAME GATE_FILE GATE_MAX [THREADS=16]     several of these can share NAME: a seed is claimed with a
file in runs/NAME/claims/. Results: s2.txt, s3.txt (seed x z volume core box "at" tp ...), seeds.txt, scan.txt, events.log."""
import heapq, os, subprocess, sys, time
name, gate_file, gate_max = sys.argv[1], sys.argv[2], float(sys.argv[3]); threads = sys.argv[4] if len(sys.argv) > 4 else '16'
here = os.path.dirname(os.path.abspath(__file__)); run = os.path.join(here, 'runs', name); os.makedirs(os.path.join(run, 'claims'), exist_ok=True)
PAIRS, TOP, BOX, MARGIN = int(os.environ.get('PAIRS', '600000')), int(os.environ.get('TOP', '400')), 1536, 48
SCANNER, PLACES, MIN_SCORE, MIN_CUT = os.environ.get('SCANNER', 'cavepairs3'), os.environ.get('PLACES', '15000'), int(os.environ.get('MIN_SCORE', '0')), os.environ.get('MIN_CUT', '0')   # MIN_CUT: the scanner stops early on a seed whose cut is lower   # MIN_SCORE: the scanner score a site needs for stage 2   # cavepairs3: terrain first, fine tables; cavepairs: the first scanner
S2_MIN = float(os.environ.get('S2_MIN', '12e6'))   # formula caverns kept in s2.txt
RATIO, FLOOR, PER_SEED = float(os.environ.get('RATIO', '0')), float(os.environ.get('FLOOR', '17e6')), int(os.environ.get('PER_SEED', '40'))
TERRAIN_DIV = float(os.environ.get('TERRAIN_DIV', '92'))
O_WEIGHT = float(os.environ.get('O_WEIGHT', '1'))   # FO_MIN is a floor on F + O_WEIGHT x O. In the GPU-gated runs F + 2 O >= 102M kept 56% of the work of F + O >= 63M, 18 of the 19 caverns of 38M+ and all 7 of 40M+
CUT_FREE = float(os.environ.get('CUT_FREE', '-inf'))   # seeds with a gate value at or under this are scanned in full whatever their cut: of 158 seeds of final score <= -0.60 that the cut had stopped, a full scan found 3 caverns of 36M+ (19 per 1000 seeds, 2.6 times the search's average per worker-second)
GATE_K, GATE_REF, FO_FLOOR = float(os.environ.get('GATE_K', '0')), float(os.environ.get('GATE_REF', '-0.62')), float(os.environ.get('FO_FLOOR', '0'))   # the exact rule's floor moves with the seed's gate score: F + O_WEIGHT x O >= max(FO_FLOOR, FO_MIN + GATE_K x (gate - GATE_REF)).
# (FO_FLOOR: the floor never goes under this. Without it the seeds with the best gate scores had sites of F + 2 O under 105M measured, which nothing measured before says are worth it.)
# The gate score still counts at the exact stage (it is independent of F and O: over 74,000 measured sites of runs g13 and g14, -0.28 +- 0.10 per sd for a cavern of 46M+): a site of a seed that only just
# made the list has to show more. With GATE_K=150e6, FO_MIN=107e6 and FO_FLOOR=105e6 (O_WEIGHT 2), 74% of the measuring time of the plain 105M floor kept 49 of the 50 caverns of 46M+, all 18 of 48M+ and 933 of the 1,002 of 40M+.
FO_MIN = float(os.environ.get('FO_MIN', '0'))   # ... and only if cluster + open volume reach this: of 6,367 caverns measured without it, F + O >= 63M kept 36% of the work, 36 of the 41 of 36M+ and all 10 of 38M+
F_MID, O_MID, F_LOW, O_HIGH = (float(os.environ.get(k, 'inf')) for k in ('F_MID', 'O_MID', 'F_LOW', 'O_HIGH'))   # the exact measure also for a smaller cluster with much open volume around it
def path(f): return os.path.join(run, f)
def append(f, lines):
    with open(path(f), 'a') as h: h.write(''.join(l + '\n' for l in lines))
best_seen, s3_offset = float(os.environ.get('BEST_START', '22022610')), 0
def best_so_far():
    global best_seen, s3_offset
    try:
        with open(path('s3.txt'), 'rb') as f:
            f.seek(s3_offset); chunk = f.read()
    except FileNotFoundError: return best_seen
    end = chunk.rfind(b'\n') + 1; s3_offset += end
    for l in chunk[:end].decode('ascii', 'replace').splitlines():
        w = l.split()
        if len(w) > 4:
            try: best_seen = max(best_seen, float(w[3]))
            except ValueError: pass
    return best_seen
heap, offset = [], 0   # the seeds read from the gate file and not tried yet, best first; how far the file has been read
def claim():
    """The unclaimed seed with the lowest gate value (at most gate_max), or None. Lines of seedgate3 carry a terrain
    phase score as their 9th word: it counts too. Only the lines added since the last call are read."""
    global offset
    try:
        with open(gate_file, 'rb') as f:
            f.seek(offset); chunk = f.read()
    except FileNotFoundError: return None
    end = chunk.rfind(b'\n') + 1; offset += end
    for l in chunk[:end].decode('ascii', 'replace').splitlines():
        w = l.split()
        if len(w) < 2: continue
        try: value = float(w[1]) + (float(w[8]) / TERRAIN_DIV if len(w) >= 9 else 0.0)   # the table gate, and the terrain phase score in the same units (seedgate3: 92 per unit of log yield; seedgate4's wide gate: 250)
        except ValueError: continue
        if value <= gate_max: heapq.heappush(heap, (round(value, 4), w[0]))
    while heap:
        gate, seed = heapq.heappop(heap)
        try: os.close(os.open(path(f'claims/{seed}'), os.O_CREAT | os.O_EXCL | os.O_WRONLY)); return seed, gate
        except FileExistsError: continue
    return None
while True:
    got = claim()
    if got is None:
        if os.environ.get('ONCE'): break   # a fixed list of seeds: done
        time.sleep(20); continue
    seed, gate = got; t0 = time.time()
    scan = subprocess.run([here + '/' + SCANNER, seed, str(PAIRS), str(TOP), threads] + ([PLACES, MIN_CUT if gate > CUT_FREE else '0'] if SCANNER != 'cavepairs' else []), capture_output=True, text=True)
    s1 = [l for l in scan.stdout.splitlines() if len(l.split()) > 3 and int(l.split()[3]) >= MIN_SCORE]; t1 = time.time()
    append('scan.txt', [l for l in scan.stderr.splitlines() if l.startswith('seed ')])   # the scanner's own account of the seed
    s2 = subprocess.run([here + '/formulacave', threads, str(BOX), str(MARGIN)], input=''.join(l + '\n' for l in s1), capture_output=True, text=True).stdout.splitlines() if s1 else []
    t2 = time.time(); best = best_so_far(); cand = []; top_formula = 0.0
    for l in s2:
        w = l.replace('|', ' ').split()   # seed x z cluster open x0 x1 y0 y1 z0 z1 cx cy cz ...
        top_formula = max(top_formula, float(w[3]))
        F, O = float(w[3]), float(w[4])
        if F + O_WEIGHT * O >= max(FO_FLOOR, FO_MIN + GATE_K * (gate - GATE_REF)) and (F >= max(FLOOR, RATIO * best) or (F >= F_MID and O >= O_MID) or (F >= F_LOW and O >= O_HIGH)): cand.append((F, f'{w[0]} {w[11]} {w[12]} {w[13]} formula {w[3]} open {w[4]} gate {gate}\n'))
    cand.sort(reverse=True)
    spaced = []                               # one start per cavern: skip a site within 400 blocks of a better one
    for c in cand:
        w = c[1].split()
        if all(abs(int(w[1]) - int(k[1].split()[1])) > 400 or abs(int(w[3]) - int(k[1].split()[3])) > 400 for k in spaced): spaced.append(c)
    cand = spaced
    append('s2.txt', [l for l in s2 if float(l.replace('|', ' ').split()[3]) >= S2_MIN])    # only the big ones are worth keeping
    if cand:
        out = subprocess.run([here + '/native/cavemeasure', threads], input=''.join(c[1] for c in cand[:PER_SEED]), capture_output=True, text=True).stdout
        results = [l for l in out.splitlines() if l.strip()]
        append('s3.txt', results)
        for r in results:
            w = r.split()
            if float(w[3]) > best:
                best = float(w[3])
                append('events.log', [f'BEST {int(best)} seed={w[0]} x={w[5]}..{w[6]} y={w[7]}..{w[8]} z={w[9]}..{w[10]} tp={w[12]},{w[13]},{w[14]} {time.strftime("%FT%TZ", time.gmtime())}'])
    append('seeds.txt', [f'{seed} {gate} {top_formula:.0f} {len(cand)} {time.time() - t0:.1f} {t1 - t0:.1f} {t2 - t1:.1f} {time.time() - t2:.1f} {len(s1)}'])   # ... seconds: all, scanner, formula, exact; sites to stage 2
