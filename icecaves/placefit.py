#!/usr/bin/env python3
"""icecaves/placefit.py: a ranking index for the scanner's candidates, fitted on full-evaluation dumps.

Input: files written by `ORACLE=/path/SEED.all icepairs SEED 1000000 10 THREADS 100000` (every place under ice caves
climate looked at closely, with the features of icepairs.cpp's FEATURES). Outcome: the fine look's score (the pure
cluster, or the ice cluster with at most 8% outside) of at least --good points. Logistic fits on every other seed,
capture on the rest:
  pair     what is known of a pair before any place is visited (cave noise means, its own weirdness)
  climate  what the climate at the place adds (temperature, continentalness, weirdness, quarters, sea floor)
  look     the first look's score, max(2 x sealed, pure): the ranking in use
  coarse   the first look's four numbers
  all      everything
usage: placefit.py [--good 300] [--write index.txt] [--model coarse] FILE.all ...     (--write: that model's coefficients, for INDEX=)"""
import sys
import numpy as np

args = sys.argv[1:]; good = 300; out = None; chosen = 'coarse'
for flag in ('--good', '--write', '--model'):
    if flag in args:
        i = args.index(flag); v = args[i + 1]; del args[i:i + 2]
        if flag == '--good': good = int(v)
        elif flag == '--write': out = v
        else: chosen = v
FEATURES = ['look', 'pure', 'sealed', 'ice', 'total', 'quarters', 'floor', 't', 'cold2', 'c', 'w8', 'w4', 'cheese8', 'cheese12', 'cheese16', 'cheese20', 'low8', 'low12', 'low16', 'low20', 'own8', 'own12', 'select', 'mean', 'wbox']
PAIR = ['cheese8', 'cheese12', 'cheese16', 'cheese20', 'low8', 'low12', 'low16', 'low20', 'own8', 'own12', 'select']
CLIMATE = ['quarters', 'floor', 't', 'cold2', 'c', 'w8', 'w4']; COARSE = ['pure', 'sealed', 'ice', 'total']
def load(path):
    rows = []
    for l in open(path):
        f = l.split()
        if 'features' not in f: continue
        k = f.index('features'); pure, ice, total = int(f[4]), int(f[6]), int(f[8])
        rows.append([max(pure, ice if (total - ice) * 100 <= 8 * total else 0)] + [float(x) for x in f[k + 1:k + 1 + len(FEATURES)]])
    return np.array(rows)
data = [load(p) for p in args]; data = [d for d in data if len(d)]
train = np.concatenate(data[0::2]); test = np.concatenate(data[1::2]) if len(data) > 1 else train
col = {n: i + 1 for i, n in enumerate(FEATURES)}
print(f'{len(data)} seeds; fit on {len(train):,} places, tested on {len(test):,}; good (fine score {good}+): {int((train[:, 0] >= good).sum())} and {int((test[:, 0] >= good).sum())}')
def design(d, feats): return np.column_stack([np.ones(len(d))] + [d[:, col[n]] for n in feats])
def logistic(X, y):
    mu = X[:, 1:].mean(0); sd = X[:, 1:].std(0) + 1e-9; Z = np.column_stack([np.ones(len(X)), (X[:, 1:] - mu) / sd])
    b = np.zeros(Z.shape[1]); b[0] = np.log(max(y.mean(), 1e-9))
    for it in range(80):
        p = 1 / (1 + np.exp(-np.clip(Z @ b, -30, 30))); H = Z.T @ (Z * (p * (1 - p))[:, None]) + 1e-4 * np.eye(Z.shape[1]); step = np.linalg.solve(H, Z.T @ (y - p) - 1e-4 * b); b += np.clip(step, -2, 2)
        if np.abs(step).max() < 1e-7: break
    raw = np.concatenate([[b[0] - (b[1:] * mu / sd).sum()], b[1:] / sd]); return raw, b
def capture(score, y, shares):
    order = np.argsort(-score, kind='stable'); total = max(y.sum(), 1)
    return '  '.join(f'{100 * s:g}%: {y[order[:max(1, int(s * len(y)))]].sum() / total:.2f}' for s in shares)
ytrain = (train[:, 0] >= good).astype(float); ytest = (test[:, 0] >= good).astype(float); big = (test[:, 0] >= good + 150).astype(float)
print(f'share of the good places among the best x% of the places under ice, by index (in brackets: of those of {good + 150}+):')
models = {'pair': PAIR, 'pair + climate': PAIR + CLIMATE, 'look': ['look'], 'coarse': COARSE, 'coarse + climate': COARSE + CLIMATE, 'all': COARSE + CLIMATE + PAIR}
for name, feats in models.items():
    raw, std = logistic(design(train, feats), ytrain); score = design(test, feats) @ raw
    shares = (0.1, 0.3, 0.5, 0.7) if name == 'pair' else (0.005, 0.01, 0.02, 0.05, 0.1)
    print(f'   {name:17s} {capture(score, ytest, shares)}   [{capture(score, big, shares[1:3])}]')
    if name in ('pair', 'all'): print('        weights per standard deviation: ' + '  '.join(f'{n} {w:+.2f}' for n, w in sorted(zip(feats, std[1:]), key=lambda t: -abs(t[1]))[:10]))
    if name == chosen and out:
        open(out, 'w').write(f'intercept {raw[0]:.6g}\n' + ''.join(f'{n} {w:.6g}\n' for n, w in zip(feats, raw[1:]))); print('   wrote', out)
