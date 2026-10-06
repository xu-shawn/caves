#!/usr/bin/env python3
"""The rarity of big caverns in one run of the search: how many distinct exact caverns of at least V blocks a processed
seed has, from exact measurements of a sample of its formula caverns.
  DIR/s2.txt     every formula cavern of 12M+ of the processed seeds (formulacave lines)
  DIR/exact.txt  cavemeasure lines whose rest holds "formula F bin B of N": bin 20 = all of them measured, a lower bin =
                 a random sample of the N distinct formula caverns of that 2M bin (weight N / sample size)
An exact cavern counts once, for its biggest formula cavern: a sample whose exact cavern holds a bigger formula cavern
of the same seed is dropped. Prints the table and returns the points for the plots (import tail)."""
import sys, numpy as np

def load(d):
    by_seed = {}
    for l in open(d + '/s2.txt'):
        w = l.replace('|', ' ').split(); by_seed.setdefault(w[0], []).append((float(w[3]), int(w[11]), int(w[12]), int(w[13])))
    distinct = {}
    for seed, rows in by_seed.items():
        kept = []
        for f, x, y, z in sorted(rows, reverse=True):
            if all(abs(x - k[1]) > 400 or abs(z - k[3]) > 400 for k in kept): kept.append((f, x, y, z))
        distinct[seed] = kept
    samples = {}
    for l in open(d + '/exact.txt'):
        w = l.split()
        if 'formula' not in w: continue
        f = float(w[w.index('formula') + 1]); b = int(w[w.index('bin') + 1]); n = int(w[w.index('of') + 1])
        samples.setdefault(b, []).append(dict(seed=w[0], x=int(w[1]), z=int(w[2]), v=float(w[3]), box=[int(v) for v in w[5:11]], f=f, n=n))
    rows = []
    for b, group in samples.items():
        for s in group:
            weight = 1.0 if b >= 20 else s['n'] / len(group)
            owned = any(f > s['f'] + 1 and s['box'][0] <= x <= s['box'][1] and s['box'][4] <= z <= s['box'][5] for f, x, y, z in distinct.get(s['seed'], []))
            rows.append((s['v'] / 1e6, s['f'] / 1e6, weight, b, owned, s['seed'], s['box']))
    # the 20M+ group is complete, so two of its formula caverns in one exact cavern show up as the same volume twice
    seen = set(); out = []
    for r in sorted(rows, key=lambda r: -r[1]):
        k = (r[5], r[0])
        if r[3] >= 20 and k in seen: continue
        seen.add(k); out.append(r)
    return len(by_seed), distinct, out

def tail(rows, seeds, thresholds):
    """distinct exact caverns >= V per seed, with a rough 1-sigma (weighted Poisson)"""
    n = []; e = []
    for t in thresholds:
        w = np.array([r[2] for r in rows if r[0] >= t and not r[4]])
        n.append(w.sum() / seeds); e.append(np.sqrt((w ** 2).sum()) / seeds)
    return np.array(n), np.array(e)

def fit(x, y, err):
    """weighted least squares of log10 y = a + b x"""
    ok = y > 0; x = x[ok]; ly = np.log10(y[ok]); w = (y[ok] / np.maximum(err[ok], 1e-12)) ** 2
    b, a = np.polyfit(x, ly, 1, w=np.sqrt(w)); return a, b

if __name__ == '__main__':
    d = sys.argv[1]; seeds, distinct, rows = load(d)
    print(f'{seeds} seeds, {len(rows)} exact measurements, {sum(r[4] for r in rows)} of them inside a bigger formula cavern\'s exact cavern')
    T = np.arange(14, 33, 2.0); n, e = tail(rows, seeds, T)
    for t, a, b in zip(T, n, e):
        parts = ' '.join(f'{bb}:{sum(r[2] for r in rows if r[0] >= t and not r[4] and r[3] == bb) / seeds:.4f}' for bb in (12, 14, 16, 18, 20))
        print(f'  exact >= {t:4.0f}M: {a:8.4f} +- {b:.4f} per seed   from formula bins {parts}')
    for lo, hi in ((20, 30), (22, 30)):
        m = (T >= lo) & (T <= hi); a, b = fit(T[m], n[m], e[m]); print(f'  fit over {lo}-{hi}M: log10 N = {a:.2f} {b:+.3f} V   (one decade per {-1 / b:.2f}M)')
    form = np.array([f for rows_ in distinct.values() for f, x, y, z in rows_]) / 1e6
    for t in (16, 18, 20, 22, 24): print(f'  formula >= {t}M: {np.sum(form >= t) / seeds:.3f} per seed')
