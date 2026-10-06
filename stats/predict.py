#!/usr/bin/env python3
"""The rarity curve of exact caverns from the formula caverns of a run, for runs that measure only the biggest formula
caverns exactly (a high FLOOR): every distinct formula cavern F of a seed counts P(exact >= V | F), taken from the
exact / formula ratios measured so far for formula caverns of about that size (within 2M; all runs pooled: a measurement
is unbiased given F, because what gets measured is chosen by F alone).
  N(>= V per seed) = sum over formula caverns of P(ratio >= V / F) / seeds
import predict; predict.pool(files) -> (F, ratio) arrays; predict.curve(seed rows, s2 file, pool, thresholds)"""
import numpy as np

def pool(s3_files):
    f, r = [], []
    for path in s3_files:
        seen = set()
        for l in open(path):
            w = l.split()
            if 'formula' not in w or float(w[3]) <= 0: continue
            k = (w[0], w[3])
            if k in seen: continue          # two formula caverns in one exact cavern: once
            seen.add(k); F = float(w[w.index('formula') + 1]); f.append(F / 1e6); r.append(float(w[3]) / F)
    return np.array(f), np.array(r)

def distinct(s2_file, keep):
    by_seed = {}
    for l in open(s2_file):
        w = l.replace('|', ' ').split()
        if w[0] in keep: by_seed.setdefault(w[0], []).append((float(w[3]) / 1e6, int(w[11]), int(w[13])))
    out = []
    for seed, rows in by_seed.items():
        kept = []
        for r in sorted(rows, reverse=True):
            if all(abs(r[1] - k[1]) > 400 or abs(r[2] - k[2]) > 400 for k in kept): kept.append(r)
        out += [k[0] for k in kept]
    return np.array(out)

def curve(formula, seeds, pooled, thresholds, floor=19.0, width=2.0):
    """predicted distinct exact caverns >= V per seed, from the formula caverns (million blocks) of `seeds` seeds"""
    pf, pr = pooled; formula = formula[formula >= floor]; out = []
    edges = np.arange(floor, max(formula.max(), floor) + 1.0, 1.0)   # formula bins of 1M, each with the ratios measured within `width` of its middle
    for t in thresholds:
        total = 0.0
        for lo in edges:
            n = np.sum((formula >= lo) & (formula < lo + 1.0))
            if n == 0: continue
            mid = lo + 0.5; ratios = pr[np.abs(pf - mid) <= width]
            if len(ratios) < 30: ratios = pr[np.abs(pf - mid) <= 2 * width]
            if len(ratios) == 0: continue
            total += n * np.mean(ratios >= t / mid)
        out.append(total / seeds)
    return np.array(out)
