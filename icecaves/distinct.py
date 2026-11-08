#!/usr/bin/env python3
"""icecaves/distinct.py: what the scanner lists on a set of seeds, each cluster once.
For runs/recall/NAME (recall.sh with SEEDS=file): the places with a fine score (pure cluster, or ice cluster with at most
8% outside) of 220+, 300+, 400+ and 500+ per seed, counting places within 320 blocks of a better one as the same
cluster, and the scanner's time. A cavern has about 8,400 blocks a fine point, so 300 points is a cavern of 2.5M.
usage: distinct.py NAME [NAME ...]"""
import glob, os, sys
here = os.path.dirname(os.path.abspath(__file__))
for name in sys.argv[1:]:
    O = f'{here}/runs/recall/{name}'; counts = [0, 0, 0, 0]; seeds = 0; best = 0
    for path in glob.glob(f'{O}/*.txt'):
        if os.path.basename(path) == 'times.txt': continue
        seeds += 1; rows = []
        for q in open(path):
            f = q.split()
            if len(f) < 21: continue
            ice, total, pure = int(f[4]), int(f[6]), int(f[8])
            if ice > pure and (total - ice) * 100 <= 8 * total: rows.append((ice, float(f[16]), float(f[17])))
            else: rows.append((pure, float(f[19]), float(f[20])))
        rows.sort(reverse=True); kept = []
        for s, x, z in rows:
            if s < 220: break
            if any(abs(x - kx) <= 320 and abs(z - kz) <= 320 for _, kx, kz in kept): continue
            kept.append((s, x, z))
        for k, t in enumerate((220, 300, 400, 500)): counts[k] += sum(1 for s, _, _ in kept if s >= t)
        best = max(best, kept[0][0] if kept else 0)
    times = [float(l.split()[1]) for l in open(f'{O}/times.txt')] if os.path.exists(f'{O}/times.txt') else [0]
    t = sum(times) / max(len(times), 1)
    print(f'{name}: {seeds} seeds, scanner {t:.1f} s a seed; distinct clusters a seed: ' + ', '.join(f'{c / max(seeds, 1):.2f} of {th}+' for c, th in zip(counts, (220, 300, 400, 500))) + f'; of 400+ per scanner-hour: {3600 * counts[2] / max(seeds, 1) / max(t, 1e-9):.0f}; best {best}')
