#!/usr/bin/env python3
"""Run g13 (box cv2, RTX 4080 Super + 61 CPU threads, $0.295 an hour) by stretch of its settings: distinct exact caverns
of at least V per box-hour and per dollar, next to the last run of the old box (g12, $0.159 an hour). The stretches come
from runs/g13/settings.txt ("from seed line N" at each restart of the workers); a cavern belongs to the stretch its seed
was scanned in. Data: stats/now/g13/ (pulled from the box), stats/now/g12/, hits/cv1_s3.txt.
usage: curve_g13.py [PRICE=0.295]"""
import sys, os, re, calendar, time, collections, math
here = os.path.dirname(os.path.abspath(__file__)); price = float(sys.argv[1]) if len(sys.argv) > 1 else 0.295
def stamp(s): return calendar.timegm(time.strptime(s, '%Y-%m-%dT%H:%M:%SZ'))
seeds = [l.split()[0] for l in open(f'{here}/now/g13/seeds.txt') if l.strip()]; line_of = {s: i for i, s in enumerate(seeds)}
caves = collections.defaultdict(set)
for l in open(f'{here}/now/g13/s3.txt'):
    w = l.split()
    if len(w) > 14 and w[0] in line_of and int(w[3]) > 0: caves[w[0]].add(int(w[3]))
marks = []   # (time, seed line, text) of every restart of the workers
for l in open(f'{here}/now/g13/settings.txt'):
    m = re.search(r'from seed line (\d+)', l)
    if m and ' workers: ' in l: marks.append((stamp(l.split()[0]), int(m.group(1)), l))
end = stamp(open(f'{here}/now/g13/pulled_at.txt').read().strip())
names = ['A  GPU gate, workers as before', 'B  + scanner terrain on the GPU', None, 'C  + exact densities on the GPU', 'C', 'D  + density kernel in two passes', 'D',
         'E  + formula stage on the GPU', 'F  + scanner tables on the GPU (and gate trials)', 'F', 'F', 'G  + faster density and table kernels', 'H  + 12-level pairs kernel, faster exact measure']
stretches = collections.OrderedDict()
for i, (t, line, text) in enumerate(marks):
    t1, line1 = (marks[i + 1][0], marks[i + 1][1]) if i + 1 < len(marks) else (end, len(seeds)); name = names[i] if i < len(names) else names[-1]
    if name is None: continue
    name = next(n for n in names if n and n.startswith(name[0] + ' ')); s = stretches.setdefault(name, [0.0, 0, []]); s[0] += (t1 - t) / 3600; s[1] += line1 - line; s[2] += [v for sd in seeds[line:line1] for v in caves.get(sd, ())]
T = (36, 38, 40, 42, 44, 46)
def row(name, hours, n, vols, cost):
    counts = [sum(v >= t * 1e6 for v in vols) for t in T]
    fit = [(t, c) for t, c in zip(T, counts) if c >= 3 and t >= 38]
    slope = ''
    if len(fit) >= 3:   # least squares of log10(count) on V, weights the counts
        W = sum(c for _, c in fit); mx = sum(c * t for t, c in fit) / W; my = sum(c * math.log10(c) for t, c in fit) / W
        b = sum(c * (t - mx) * (math.log10(c) - my) for t, c in fit) / sum(c * (t - mx) ** 2 for t, c in fit); slope = f'{-1 / b:.1f}M'
    print(f'{name:50s} {hours:5.2f} h {n:6d} seeds ({n / hours:6.0f}/h)  per hour: ' + ' '.join(f'{c / hours:6.1f}' for c in counts) + '   per dollar: ' + ' '.join(f'{c / hours / cost:6.0f}' for c in counts) + f'   counts {counts}  decade per {slope}')
print(' ' * 50 + '                                     ' + ' '.join(f'{t:5d}M+' for t in T) + '                ' + ' '.join(f'{t:5d}M+' for t in T))
# the old box's last run
old_seeds = {l.split()[0] for l in open(f'{here}/now/g12/seeds.txt') if l.strip()}; old = collections.defaultdict(set)
for l in open(f'{here}/../hits/cv1_s3.txt'):
    w = l.split()
    if len(w) > 14 and w[0] in old_seeds and int(w[3]) > 0: old[w[0]].add(int(w[3]))
row('old box, last run (g12, $0.159/h)', 1.90, len(old_seeds), [v for s in old.values() for v in s], 0.159)
for name, (hours, n, vols) in stretches.items():
    if hours > 0.05: row(name, hours, n, vols, price)
allv = [v for s in caves.values() for v in s]; hours = (end - marks[0][0]) / 3600
row('g13, all of it', hours, len(seeds), allv, price)
print('largest:', sorted(allv, reverse=True)[:6])
