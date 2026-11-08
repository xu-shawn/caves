#!/usr/bin/env python3
"""icecaves/gatefit.py: the phase gate's layers, weights, centres and windows, fitted on what fresh seeds yielded.

For the runs named (fresh seeds: runs/NAME/seeds.log and results.txt) every layer's phase is computed (icephase
phases) and a Poisson regression of a per-seed count on cos and sin of the phases gives, per layer, a weight (the log
rate between the best and the average phase, per stack) and the best phase. A run dummy absorbs the level of each run
(runs differ in which layers they were gated on, so a gated layer is only fitted inside its window: its numbers come
with wide errors once the windows are tight; layers the runs left free are fitted cleanly).

Then the windows: for layers with weights w_i, the box that loses least for a share P of seeds kept has half-widths
h_i = k / sqrt(w_i), with (2k)^(2n) / prod(w_i) = P for n layers (two stacks each); every window then costs the same
(2 pi k)^2 / 6 of log rate.

usage: gatefit.py [--target clusters350|clusters430|caverns2.3|...] [--kept 1.5e-8] [--spec OUT] [--pool] RUN [RUN ...]
  --pool: the runs share one level (same pipeline settings, different gates).
  targets: clustersN = formula pure clusters of N+ samples that were tested; cavernsX = fully-ice caverns of X M+.
  Layers gated in the spec: FORCED below (always) plus any other layer whose weight is at least --min-weight (0.2)
  at 3 standard errors. 2-D climate layers take the centre 0.5 or 0 (only cos is physical for them)."""
import math, subprocess, sys
import numpy as np

root = __file__.rsplit('/', 1)[0]
NAMES = ['T-10', 'T-8', 'W-7', 'W-6', 'W-5', 'C-9', 'C-8', 'C-7', 'C-6', 'H-8', 'H-7', 'ch-8', 'ch-7', 'ch-6', 'layer', 'flood', 'E-9', 'E-8', 'pillar-7', 'pillar-6',
         'p_rare', 'p_thick', 'entr-7', 'entr-6', 'entr-5', 'sp_rough', 'aq_spread', 'aq_barrier', 'noodle']
# how each layer is written in a gate spec: noise, octave, reference height in cells (as in icephase.cpp's LAYERS)
Y = -16.0
SPEC = {'T-10': ('temperature', -10, 0), 'W-7': ('ridge', -7, 0), 'W-6': ('ridge', -6, 0), 'ch-8': ('cave_cheese', -8, Y * (2 / 3) / 256), 'ch-7': ('cave_cheese', -7, Y * (2 / 3) / 128),
        'pillar-7': ('pillar', -7, Y * 0.3 / 128), 'layer': ('cave_layer', -8, Y * 8 / 256), 'flood': ('aquifer_fluid_level_floodedness', -7, Y * 0.67 / 128),
        'pillar-6': ('pillar', -6, Y * 0.3 / 64), 'p_rare': ('pillar_rareness', -8, Y / 256), 'p_thick': ('pillar_thickness', -8, Y / 256), 'aq_spread': ('aquifer_fluid_level_spread', -5, Y * 0.7142857142857143 / 32)}
FLAT = {'T-10', 'T-8', 'W-7', 'W-6', 'W-5', 'C-9', 'C-8', 'C-7', 'C-6', 'H-8', 'H-7', 'E-9', 'E-8'}   # sampled at y 0 only
TIED = {'T-8', 'W-5', 'ch-6', 'C-6', 'C-8', 'entr-5'}   # tied to another layer of their noise by the seed generator: never gated
# the climate layers' weights are fixed here (fitted on seeds whose climate was not gated: README); the cave layers come from the fit
FORCED = {'T-10': (0.32, 0.5), 'W-7': (0.63, 0.5), 'W-6': (0.37, 0.5)}

args = sys.argv[1:]; target = 'clusters350'; kept = 1.5e-8; out = None; least = 0.2
pool = '--pool' in args   # runs with the same pipeline settings and different gates: no level per run, so the windows' widths count too
if pool: args.remove('--pool')
for flag in ('--target', '--kept', '--spec', '--min-weight'):
    if flag in args:
        i = args.index(flag); v = args[i + 1]; del args[i:i + 2]
        if flag == '--target': target = v
        elif flag == '--kept': kept = float(v)
        elif flag == '--spec': out = v
        else: least = float(v)
runs = args
rows = []; tests = {}
for run in runs:
    for l in open(f'{root}/runs/{run}/seeds.log'):
        f = l.split()
        if len(f) >= 30 and f[15] == 'fully_ice_and_most_ice': rows.append((f[0], run))
    for l in open(f'{root}/runs/{run}/results.txt'):
        f = l.split()
        if 'seconds' not in f: continue
        j = max(k for k, x in enumerate(f) if x == '|'); tag = f[j + 1:]
        if len(tag) < 10 or tag[0] != 'S': continue
        i = f.index('seconds'); volume = int(f[3]); left = 'left' in f[i + 4:i + 6]
        tests.setdefault(f[0], []).append((int(tag[8]), (not left) and int(f[i + 3]) == volume, volume))
text = subprocess.run([f'{root}/icephase', 'phases'], input='\n'.join(r[0] for r in rows) + '\n', capture_output=True, text=True).stdout
P = {l.split()[0]: np.array([float(x) for x in l.split()[1:]]) for l in text.splitlines()}
ph = np.array([P[r[0]] for r in rows]); n = len(rows); th = 2 * np.pi * ph
if target.startswith('clusters'): size = int(target[8:]); y = np.array([sum(fp >= size for fp, ok, v in tests.get(r[0], ())) for r in rows], float)
else: size = float(target[7:]) * 1e6; y = np.array([sum(ok and v >= size for fp, ok, v in tests.get(r[0], ())) for r in rows], float)
D = [np.ones(n)] + ([] if pool else [np.array([r[1] == u for r in rows], float) for u in runs[1:]]); nd = len(D)
layers = [k for k, name in enumerate(NAMES) if name not in TIED]
cols = []
for k in layers: cols += [np.cos(th[:, 2 * k]) + np.cos(th[:, 2 * k + 1]), np.sin(th[:, 2 * k]) + np.sin(th[:, 2 * k + 1])]
X = np.column_stack(D + cols)
b = np.zeros(X.shape[1]); b[0] = math.log(max(y.mean(), 1e-9))
for it in range(300):
    mu = np.exp(np.clip(X @ b, -30, 30)); H = X.T @ (X * mu[:, None]) + 1e-6 * np.eye(X.shape[1]); step = np.linalg.solve(H, X.T @ (y - mu)); b += np.clip(step, -1, 1)
    if np.abs(step).max() < 1e-8: break
cov = np.linalg.inv(H)
print(f'{n} seeds of {len(runs)} runs; target {target}: {int(y.sum())} events')
fitted = {}
for i, k in enumerate(layers):
    name = NAMES[k]; c, s = b[nd + 2 * i], b[nd + 2 * i + 1]; sc, ss = math.sqrt(cov[nd + 2 * i, nd + 2 * i]), math.sqrt(cov[nd + 2 * i + 1, nd + 2 * i + 1])
    if name in FLAT: weight = abs(c); centre = 0.0 if c > 0 else 0.5; error = sc; phase_error = 0.0
    else: weight = math.hypot(c, s); centre = (math.atan2(s, c) / (2 * math.pi)) % 1; error = max(sc, ss); phase_error = error / max(weight, 1e-9) / (2 * math.pi)
    fitted[name] = (weight, centre, error, phase_error)
print('layer: weight per stack +- error, best phase (z)')
for name, (w, c, e, pe) in sorted(fitted.items(), key=lambda kv: -kv[1][0] / kv[1][2]):
    if w / e >= 1.5: print(f'   {name:10s} {w:.2f} +- {e:.2f} at {c:.3f}' + (f' +- {pe:.3f}' if pe else '') + f'   ({w / e:.1f})')
gate = dict(FORCED)
for name, (w, c, e, pe) in fitted.items():
    if name in FORCED or name not in SPEC: continue
    if w >= least and w / e >= 3: gate[name] = (w, c)
count = len(gate); k = 0.5 * (kept * math.prod(w for w, c in gate.values())) ** (1 / (2 * count))
loss = 2 * count * (2 * math.pi * k) ** 2 / 6
print(f'gate of {count} layers for {kept:g} of the seeds: each window costs {(2 * math.pi * k) ** 2 / 6:.3f}; {loss:.2f} lost of {2 * sum(w for w, c in gate.values()):.2f}')
order = sorted(gate, key=lambda name: (SPEC[name][0] != 'cave_cheese', SPEC[name][0], SPEC[name][1]))   # tightest noise first; a noise's layers together
parts = []
for name in order:
    w, c = gate[name]; noise, octave, at = SPEC[name]; parts.append(f'{noise},{octave},{at:.7f},{c:.3f},{min(0.5, k / math.sqrt(w)):.3f}'); print(f'   {name:10s} weight {w:.2f}: centre {c:.3f} +- {min(0.5, k / math.sqrt(w)):.3f}')
spec = ';'.join(parts); print(spec)
if out: open(out, 'w').write(spec + '\n')
