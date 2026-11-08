#!/usr/bin/env python3
"""icecaves/indexfit.py: the ranking index for the fine look, judged by clusters and not by places.

Input: full-evaluation dumps (`ORACLE=/path/SEED.all icepairs SEED PAIRS 10 1 1000000`: every place under ice caves
looked at closely, with icepairs.cpp's FEATURES). A good cluster: fine score (the pure cluster, or the ice cluster with
at most 8% outside) of --good points or more; places whose windows are within 8 steps of a better one count as the same
cluster. A ranking with a budget of N fine looks a seed catches a cluster when any of its places is among the seed's
best N. Logistic fits on every other seed, the catch on the rest.
usage: indexfit.py [--good 400] [--write index.txt --model NAME] FILE.all ..."""
import sys
import numpy as np
args = sys.argv[1:]; good = 400; out = None; chosen = None
for flag in ('--good', '--write', '--model'):
    if flag in args:
        i = args.index(flag); v = args[i + 1]; del args[i:i + 2]
        if flag == '--good': good = int(v)
        elif flag == '--write': out = v
        else: chosen = v
FEATURES = ['look', 'pure', 'sealed', 'ice', 'total', 'quarters', 'floor', 't', 'cold2', 'c', 'w8', 'w4', 'cheese8', 'cheese12', 'cheese16', 'cheese20', 'low8', 'low12', 'low16', 'low20', 'own8', 'own12', 'select', 'mean', 'wbox']
col = {n: i + 3 for i, n in enumerate(FEATURES)}
def load(path):
    rows = []
    for l in open(path):
        f = l.split()
        if 'features' not in f: continue
        k = f.index('features'); pure, ice, total = int(f[4]), int(f[6]), int(f[8])
        rows.append([max(pure, ice if (total - ice) * 100 <= 8 * total else 0), int(f[1]), int(f[2])] + [float(x) for x in f[k + 1:k + 1 + len(FEATURES)]])
    return np.array(rows)
def clusters(d):   # per good place: the id of its cluster (the best place within 8 steps, greedily)
    idx = np.where(d[:, 0] >= good)[0]; idx = idx[np.argsort(-d[idx, 0])]; heads = []; owner = {}
    for i in idx:
        for h in heads:
            if abs(d[i, 1] - d[h, 1]) <= 8 and abs(d[i, 2] - d[h, 2]) <= 8: owner[i] = h; break
        else: heads.append(i); owner[i] = i
    return owner, heads
data = [load(p) for p in args]; data = [d for d in data if len(d)]
train = np.concatenate(data[0::2]); tests = data[1::2] if len(data) > 1 else data
owners = [clusters(d) for d in tests]
print(f'{len(data)} seeds, {sum(len(d) for d in data) / len(data):,.0f} places under ice caves a seed; clusters of {good}+ in the test seeds: {sum(len(h) for _, h in owners) / len(tests):.1f} a seed, of {good + 150}+: {sum(sum(1 for i in h if d[i, 0] >= good + 150) for d, (_, h) in zip(tests, owners)) / len(tests):.1f}')
def logistic(X, y):
    mu = X.mean(0); sd = X.std(0) + 1e-9; Z = np.column_stack([np.ones(len(X)), (X - mu) / sd]); b = np.zeros(Z.shape[1]); b[0] = np.log(max(y.mean(), 1e-9))
    for it in range(80):
        p = 1 / (1 + np.exp(-np.clip(Z @ b, -30, 30))); H = Z.T @ (Z * (p * (1 - p))[:, None]) + 1e-4 * np.eye(Z.shape[1]); step = np.linalg.solve(H, Z.T @ (y - p) - 1e-4 * b); b += np.clip(step, -2, 2)
        if np.abs(step).max() < 1e-7: break
    return np.concatenate([[b[0] - (b[1:] * mu / sd).sum()], b[1:] / sd]), b[1:]
BUDGETS = (100, 200, 400, 700, 1300)
def catch(scorer):
    out = []
    for n in BUDGETS:
        got = big = 0
        for d, (owner, heads) in zip(tests, owners):
            top = set(np.argsort(-scorer(d), kind='stable')[:n].tolist()); caught = {owner[i] for i in owner if i in top}
            got += len(caught); big += sum(1 for h in caught if d[h, 0] >= good + 150)
        out.append(f'{n}: {got / len(tests):5.1f} ({big / len(tests):4.1f})')
    return '   '.join(out)
print(f'clusters of {good}+ caught a seed (of {good + 150}+) by the budget of fine looks:')
print(f'   {"the lists in use (2 x sealed or pure)":40s} {catch(lambda d: np.maximum(2 * d[:, col["sealed"]], d[:, col["pure"]]))}')
try:
    w = dict((l.split()[0], float(l.split()[1])) for l in open(__file__.rsplit('/', 1)[0] + '/index_coarse.txt'))
    print(f'   {"index_coarse.txt (in use)":40s} {catch(lambda d: sum(v * d[:, col[k]] for k, v in w.items() if k in col))}')
except OSError: pass
COARSE = ['pure', 'sealed', 'ice', 'total']
models = {'coarse': COARSE, 'coarse + wbox': COARSE + ['wbox'], 'coarse + wbox + w8 + w4': COARSE + ['wbox', 'w8', 'w4'], 'coarse + wbox + climate': COARSE + ['wbox', 'w8', 'w4', 't', 'cold2', 'c', 'quarters', 'floor'], 'coarse + wbox + climate + mean': COARSE + ['wbox', 'w8', 'w4', 't', 'cold2', 'c', 'quarters', 'floor', 'mean']}
y = (train[:, 0] >= good).astype(float)
for name, feats in models.items():
    raw, std = logistic(np.column_stack([train[:, col[n]] for n in feats]), y)
    print(f'   {name:40s} {catch(lambda d: raw[0] + sum(v * d[:, col[n]] for n, v in zip(feats, raw[1:])))}')
    print('        per standard deviation: ' + '  '.join(f'{n} {v:+.2f}' for n, v in zip(feats, std)))
    if name == chosen and out: open(out, 'w').write(f'intercept {raw[0]:.6g}\n' + ''.join(f'{n} {v:.6g}\n' for n, v in zip(feats, raw[1:]))); print('   wrote', out)
