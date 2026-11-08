#!/usr/bin/env python3
"""icecaves/docs/plots.py: the figures of ICECAVES.md, from docs/data and runs/. usage: docs/plots.py"""
import collections, math, os, subprocess, sys, tempfile
import numpy as np
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker

here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here); img = os.path.join(here, 'img'); os.makedirs(img, exist_ok=True)
INK, INK2, GRID, BLUE, ORANGE, TEAL, RULE = '#1f2328', '#59636e', '#d8dee4', '#2a78d6', '#eb6834', '#1f9d8b', '#8c959f'
plt.rcParams.update({'font.size': 9, 'axes.edgecolor': GRID, 'axes.labelcolor': INK2, 'xtick.color': INK2, 'ytick.color': INK2, 'text.color': INK, 'axes.titlesize': 10, 'axes.titlecolor': INK, 'axes.titlelocation': 'left'})
def tidy(ax, grid='y'):
    for s in ('top', 'right'): ax.spines[s].set_visible(False)
    ax.grid(True, axis=grid, color=GRID, lw=0.6); ax.set_axisbelow(True); ax.tick_params(length=2)

def caverns(run, first=0, last=None):
    """seeds, thread-seconds, fully-ice volumes (a cavern once), per-seed most ice, of the seeds [first, last) of a run"""
    rows = [l.split() for l in open(f'{root}/runs/{run}/seeds.log') if len(l.split()) >= 30][first:last]; ids = {r[0] for r in rows}; found = {}
    for l in open(f'{root}/runs/{run}/results.txt'):
        f = l.split()
        if 'seconds' not in f or f[0] not in ids: continue
        i = f.index('seconds'); volume = int(f[3])
        if 'left' in f[i + 4:i + 6] or volume == 0 or int(f[i + 3]) != volume: continue
        found.setdefault(f[0], set()).add(volume)
    return len(rows), sum(float(r[2]) + float(r[4]) + float(r[6]) for r in rows), sorted((v for s in found.values() for v in s), reverse=True), [r[0] for r in rows], found

# ---- 1. the lens, and where caverns leave it
def lens():
    fig, (a, b) = plt.subplots(1, 2, figsize=(10.4, 4.0), gridspec_kw={'width_ratios': [1.05, 1], 'wspace': 0.28})
    ys = np.arange(-56, 44, 4)   # the cells' lowest blocks
    for off, floor, color, name in ((-0.105, 48, BLUE, 'ocean (sea floor y 48)'), (-0.207, 35, ORANGE, 'deep ocean (sea floor y 35)')):
        depth = 1 - ys / 128.0 - 83 / 160.0 + off; near = np.minimum(depth, 1 - depth); out = np.where(depth < 0.2, 0.2 - depth, np.where(depth > 0.9, depth - 0.9, 0))
        reach = np.sqrt(np.maximum(near ** 2 - out ** 2, 0)); ok = (near > 0) & (near > out)   # how far outside the box the other numbers may be
        least = np.where(ok, 0.5 - reach, np.nan); a.plot(least[ys < floor - 8], ys[ys < floor - 8] + 2, color=color, lw=2)
        k = np.nanargmin(least); a.text(least[k] + 0.035, ys[k] + 2, name, color=color, fontsize=8.5, ha='left', va='center')
    a.axhline(-54, color=RULE, lw=0.8, ls=(0, (4, 3))); a.text(0.52, -52.5, 'lava below y -54', color=INK2, fontsize=8, ha='right')
    a.axvline(0.5, color=RULE, lw=0.8, ls=(0, (4, 3))); a.text(0.505, 30, 'the box:\nweirdness 0.5', color=INK2, fontsize=8, va='top')
    a.set_xlim(-0.05, 0.62); a.set_ylim(-58, 40); a.set_xlabel('lowest weirdness that is still ice caves\n(the other numbers inside the box)'); a.set_ylabel('y'); tidy(a, 'both')
    a.set_title('Ice caves reach past their box: a lens')
    y, cause = [], []
    for l in open(f'{here}/data/exits_413.txt'):
        f = l.split()
        if 'left' not in f: continue
        i = f.index('left'); t, c, w, d = (float(f[i + k]) for k in (6, 10, 12, 14))
        parts = {'weirdness': max(0.5 - w, 0), 'continentalness': max(c + 0.19, 0), 'temperature': max(t + 0.8, 0), 'depth': max(d - 0.9, 0.2 - d, 0)}
        y.append(int(f[i + 3])); cause.append(max(parts, key=parts.get))
    y = np.array(y); cause = np.array(cause); bins = np.arange(-56, 64, 8); bottom = np.zeros(len(bins) - 1)
    for name, color in (('weirdness', BLUE), ('continentalness', ORANGE), ('temperature', TEAL), ('depth', RULE)):
        h = np.histogram(y[cause == name], bins)[0]; b.barh(bins[:-1] + 4, h, height=7, left=bottom, color=color, label=f'{name} ({(cause == name).sum()})', edgecolor='white', lw=0.6); bottom += h
    b.set_xlabel('caverns'); b.set_ylabel('y of the first core block outside the ice caves'); b.set_ylim(-58, 62); b.legend(frameon=False, fontsize=8.5, title='the number at fault', title_fontsize=8.5); tidy(b, 'x')
    b.set_title(f'Where {len(y)} caverns left it')
    fig.savefig(f'{img}/lens.png', dpi=140, bbox_inches='tight'); plt.close(fig)

# ---- 2. phases
def phase_terms(p): return -np.cos(2 * np.pi * p)
def phases():
    a3 = np.array([[float(x) for x in l.split()[1:]] for l in open(f'{here}/data/phase_stats_3000.txt')]); a4 = np.array([[float(x) for x in l.split()[1:]] for l in open(f'{here}/data/phase_lens_4000.txt')])
    fig, axes = plt.subplots(1, 3, figsize=(11.0, 3.9), gridspec_kw={'wspace': 0.36})
    ax = axes[0]; x = phase_terms(a3[:, 0]) + phase_terms(a3[:, 1]); y = a3[:, 28] / 2000.0
    ax.plot(x, y, '.', color=BLUE, ms=2.2, alpha=0.5); k = np.polyfit(x, np.log(y), 1); xs = np.linspace(-2, 2, 50); ax.plot(xs, np.exp(np.polyval(k, xs)), color=INK, lw=1)
    ax.set_yscale('log'); ax.set_xlabel('temperature octave -10, both stacks:\nsum of -cos(2π phase)'); ax.set_ylabel('share of the world at -0.8 or colder, %'); ax.set_yticks([0.5, 1, 2]); ax.set_yticklabels(['0.5', '1', '2']); ax.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter()); tidy(ax, 'both')
    ax.set_title(f'Cold enough: 2 numbers, R² {np.corrcoef(x, np.log(y))[0, 1] ** 2:.2f}'); ax.text(0.04, 0.93, '3,000 random seeds', transform=ax.transAxes, color=INK2, fontsize=8)
    ax = axes[1]; x = sum(phase_terms(a3[:, k]) for k in (4, 5, 6, 7)); y = a3[:, 32] + 1
    ax.plot(x, y, '.', color=ORANGE, ms=2.2, alpha=0.5); k = np.polyfit(x, np.log(y), 1); xs = np.linspace(-4, 4, 50); ax.plot(xs, np.exp(np.polyval(k, xs)), color=INK, lw=1)
    ax.set_yscale('log'); ax.set_xlabel('weirdness octaves -7 and -6, both stacks:\nthe same sum'); ax.set_ylabel('256-block crosses with weirdness ≥ 0.5\n(of 100,000 columns)'); tidy(ax, 'both')
    ax.set_title(f'Wide lobes: 4 numbers, R² {np.corrcoef(x, np.log(y))[0, 1] ** 2:.2f}')
    # the caverns themselves, by the score of the cave-gate seeds scanned in their own order
    ax = axes[2]; n1, t1, v1, seeds1, found1 = caverns('long1'); n2, t2, v2, seeds2, found2 = caverns('e2_gate'); seeds = seeds1 + seeds2; found = {**found1, **found2}
    out = subprocess.run([f'{root}/icephase', 'phases'], input='\n'.join(seeds) + '\n', capture_output=True, text=True).stdout
    P = {l.split()[0]: np.array([float(x) for x in l.split()[1:]]) for l in out.splitlines()}
    def score(s): c = phase_terms(P[s]); return 0.32 * (c[0] + c[1]) - 0.12 * (c[2] + c[3]) + 0.63 * (c[4] + c[5]) + 0.37 * (c[6] + c[7])
    sc = np.array([score(s) for s in seeds]); two = np.array([sum(v >= 2e6 for v in found.get(s, ())) for s in seeds]); order = np.argsort(sc); groups = np.array_split(order, 5)
    xs = [sc[g].mean() for g in groups]; ys = [two[g].mean() for g in groups]; es = [math.sqrt(max(two[g].sum(), 1)) / len(g) for g in groups]
    ax.errorbar(xs, ys, yerr=es, fmt='o-', color=TEAL, ms=5, lw=1.6, capsize=2); ax.set_xlabel('phase score of the seed\n(fifths of the seeds)'); ax.set_ylabel('caverns of 2M+, ice caves throughout,\nper seed'); ax.set_ylim(0, None); tidy(ax, 'both')
    ax.set_title(f'Found in {len(seeds):,} scanned seeds'); ax.text(0.04, 0.80, 'cave-gate seeds,\nthe cave gate\'s order', transform=ax.transAxes, color=INK2, fontsize=8)
    fig.savefig(f'{img}/phases.png', dpi=140, bbox_inches='tight'); plt.close(fig)

# ---- 3. the tail by source of seeds
def tail(sources):
    fig, (a, b) = plt.subplots(1, 2, figsize=(10.6, 4.1), gridspec_kw={'width_ratios': [1.25, 1], 'wspace': 0.24}); rates = []; labels = []
    for (name, runs, color) in sources:
        n = 0; t = 0; v = []
        for run, first, last in runs: k, s, vv, _, _ = caverns(run, first, last); n += k; t += s; v += vv
        v = sorted(v); ys = [(len(v) - i) / n for i in range(len(v))]
        a.step([x / 1e6 for x in v], ys, where='post', color=color, lw=1.8)
        over = [x - 2e6 for x in v if x >= 2e6]; scale = sum(over) / len(over)
        labels.append((ys[-1], v[-1] / 1e6, f'{name}: {n:,} seeds, an e-fold per {scale / 1e6:.2f}M', color))
        rates.append((name, color, [3600 * sum(x >= thr for x in v) / t for thr in (2e6, 2.5e6, 3e6)], [sum(x >= thr for x in v) for thr in (2e6, 2.5e6, 3e6)]))
    a.set_yscale('log'); a.set_xlim(2, 5.9); a.set_xlabel('cavern that is ice caves throughout, million blocks'); a.set_ylabel('caverns of at least that size per seed'); tidy(a, 'both')
    a.set_ylim(1.5e-4, 0.4)
    for k, (y, x, text, color) in enumerate(labels): a.text(0.02, 0.02 + 0.06 * (len(labels) - 1 - k), text, transform=a.transAxes, color=color, fontsize=8, ha='left', va='bottom')
    a.set_title('How many caverns per seed reach a size')
    w = 0.8 / len(rates)
    for k, (name, color, r, counts) in enumerate(rates):
        xs = np.arange(3) + (k - (len(rates) - 1) / 2) * w; b.bar(xs, r, width=w * 0.92, color=color)
        for x, y, c in zip(xs, r, counts): b.text(x, max(y, 0.32), (f'{y:.1f}' if y < 10 else f'{y:.0f}') if c else '0', ha='center', va='bottom', fontsize=7.5, color=INK2)
    b.set_xticks(np.arange(3)); b.set_xticklabels(['2M+', '2.5M+', '3M+']); b.set_ylabel('caverns per thread-hour'); b.set_yscale('log'); b.set_ylim(0.3, None); tidy(b)
    b.set_title('The same per thread-hour of the laptop')
    fig.savefig(f'{img}/tail.png', dpi=140, bbox_inches='tight'); plt.close(fig)

# ---- 3b. the tail by policy (a run is cut where its notes.txt say the policy changed; each curve starts where its counts are complete)
GATE_CPU = 1.7   # thread-seconds of CPU gate a seed kept
POLICIES = [('before: a million pairs, their own weirdness', [('prod1', 0, None), ('prod3', 0, None)], 3.0e6, RULE),
            ('weirdness tables, small windows; pure clusters tested', [('box1', 0, None), ('box2', 0, 1954), ('prod4', 0, None), ('prod5', 0, 3409)], 3.5e6, ORANGE),
            ('nearly pure clusters tested too', [('box2', 1954, None), ('box3', 0, None), ('prod5', 3409, None)], 4.0e6, BLUE),
            ('largest only', [('box4', 0, None), ('prod6', 0, None)], 4.7e6, TEAL)]
def pooled(parts):
    n = 0; t = 0; v = []
    for run, first, last in parts:
        if os.path.exists(f'{root}/runs/{run}/results.txt') and os.path.exists(f'{root}/runs/{run}/seeds.log'): k, s, vv, _, _ = caverns(run, first, last); n += k; t += s; v += vv
    return n, t, sorted(v, reverse=True)
def tailnow():
    fig, (a, b) = plt.subplots(1, 2, figsize=(10.6, 4.1), gridspec_kw={'wspace': 0.26})
    for name, parts, least, color in POLICIES:
        n, t, v = pooled(parts)
        if n < 50: continue
        w = [x for x in v if x >= least]; a.step([x / 1e6 for x in w], [(k + 1) / n for k in range(len(w))], where='post', color=color, lw=1.8, label=f'{name}: {n:,} seeds')
    for name, parts, least, color in (('pure clusters tested (box1)', [('box1', 0, None)], 3.5e6, ORANGE), ('nearly pure too (box3)', [('box3', 0, None)], 4.0e6, BLUE), ('largest only (box4)', [('box4', 0, None)], 4.7e6, TEAL)):
        n, t, v = pooled(parts)
        if n < 50: continue
        w = [x for x in v if x >= least]; b.step([x / 1e6 for x in w], [3600 * (k + 1) / t for k in range(len(w))], where='post', color=color, lw=1.8, label=f'{name}: {t / n:.1f} thread-s a seed')
    for ax, what in ((a, 'caverns of at least that size, per seed'), (b, 'the same per thread-hour of the rented box')):
        ax.set_yscale('log'); ax.set_xlabel('cavern that is ice caves throughout, million blocks'); ax.set_ylabel(what); tidy(ax, 'both'); ax.legend(fontsize=7, frameon=False, loc='lower left')
    a.set_title('How many caverns reach a size'); b.set_title('For the compute, on the box')
    fig.savefig(f'{img}/tail_now.png', dpi=140, bbox_inches='tight'); plt.close(fig)

# ---- 3c. what bounds the largest: the fate of the candidates by size, where the failures leave, and the patch of ice caves
def bounds():
    import collections
    T = []
    for run in ('box1', 'box2', 'box3', 'prod4', 'prod5'):
        p = f'{root}/runs/{run}/results.txt'
        if not os.path.exists(p): continue
        for l in open(p):
            f = l.split()
            if 'seconds' not in f: continue
            j = max(k for k, x in enumerate(f) if x == '|'); tag = f[j + 1:]
            if len(tag) < 10 or tag[0] not in 'SN': continue
            i = f.index('seconds'); v = int(f[3]); left = 'left' in f[i + 4:i + 6]; score = max(int(tag[2]), int(tag[3])); fault = None; y = 0
            if left:
                k = f.index('left'); y = int(f[k + 3]); g = {f[q]: float(f[q + 1]) for q in range(k + 5, j - 1, 2) if f[q] in ('t', 'h', 'c', 'w', 'depth')}
                far = {'weirdness': max(0, 0.5 - g['w'], g['w'] - 1.1), 'continentalness': max(0, g['c'] + 0.19, -1.05 - g['c']), 'temperature': max(0, g['t'] + 0.8), 'depth': max(0, 0.2 - g['depth'], g['depth'] - 0.9)}; fault = max(far, key=far.get)
            T.append((score, (not left) and v > 0 and int(f[i + 3]) == v, left, fault, y, v))
    fig, (a, b, c) = plt.subplots(1, 3, figsize=(12.0, 3.8), gridspec_kw={'wspace': 0.42, 'width_ratios': [1, 1.15, 1]})
    edges = [450, 500, 550, 600, 650, 700, 750, 800, 900]; xs = []; ys = []; es = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = [t for t in T if lo <= t[0] < hi]
        if len(m) >= 30: p = sum(t[1] for t in m) / len(m); xs.append((lo + hi) / 2); ys.append(100 * p); es.append(100 * math.sqrt(max(p * (1 - p), 1e-9) / len(m)))
    a.errorbar(xs, ys, yerr=es, fmt='o-', color=BLUE, ms=4, lw=1.6, capsize=2); a.set_xlabel('points of the scanner\'s cluster\n(8,400 blocks each)'); a.set_ylabel('exact tests that stay in the ice caves, %'); a.set_ylim(0, None); tidy(a, 'both'); a.set_title('The larger, the less often sealed')
    big = [t for t in T if t[2] and t[0] >= 700]; names = ['weirdness', 'continentalness', 'temperature', 'depth']; colors = [BLUE, ORANGE, TEAL, RULE]; bands = [(-64, -44, 'y -64..-45\n(floor)'), (-44, -24, '-44..-25'), (-24, 0, '-24..-1'), (0, 24, '0..23'), (24, 200, '24+\n(roof)')]
    left = np.zeros(len(bands))
    for name, color in zip(names, colors):
        w = np.array([100 * sum(1 for t in big if lo <= t[4] < hi and t[3] == name) / len(big) for lo, hi, _ in bands]); b.barh(range(len(bands)), w, left=left, color=color, label=name, height=0.72); left += w
    b.set_yticks(range(len(bands))); b.set_yticklabels([x[2] for x in bands], fontsize=8); b.set_xlabel(f'share of the {len(big):,} failures\nof 700+ points, %'); b.legend(fontsize=7.5, frameon=False, loc='upper right', title='the number at fault', title_fontsize=7.5); tidy(b, 'x'); b.set_title('Where the core leaves')
    R = []
    for l in open(f'{here}/data/lobe_3118.txt'):
        f = l.split(); k = [i for i, x in enumerate(f) if x == '|']; rest = f[k[4] + 1:]; R.append((int(f[k[0] + 2]) / 1e6, rest[0], int(rest[1]), int(rest[3])))
    n78 = sum(1 for r in R if r[1] == 'left' and 700 <= r[3] < 800); wt = lambda r: 1935 / n78 if (r[1] == 'left' and r[3] < 800) else 1.0   # (the failures of 700-800 points are a sample of 600 of 1,935)
    pe = [0.15, 0.3, 0.4, 0.5, 0.6, 0.7, 0.85, 1.4]; xs = []; ys = []; es = []
    for lo, hi in zip(pe[:-1], pe[1:]):
        m = [r for r in R if r[3] >= 700 and r[1] in ('full', 'left') and lo <= r[0] < hi]; n = sum(wt(r) for r in m); p = sum(1 for r in m if r[1] == 'full')
        if n >= 50: xs.append((lo + hi) / 2); ys.append(100 * p / n); es.append(100 * math.sqrt(max(p, 1)) / n)
    c.errorbar(xs, ys, yerr=es, fmt='o-', color=TEAL, ms=4, lw=1.6, capsize=2); c.set_xlabel('patch that is ice caves\nfrom y -56 to 16, km²'); c.set_ylabel('candidates of 700+ points that stay, %'); c.set_ylim(0, None); tidy(c, 'both'); c.set_title('A larger patch helps')
    fig.savefig(f'{img}/bounds.png', dpi=140, bbox_inches='tight'); plt.close(fig)

# ---- 4. the record cavern from above
def record(seed, x, y, z, name):
    with tempfile.TemporaryDirectory() as tmp:
        dump = os.path.join(tmp, 'cavern.bin'); subprocess.run([f'{root}/icemeasure', '1'], input=f'{seed} {x} {y} {z}\n', capture_output=True, text=True, env=dict(os.environ, CAVE_DUMP=dump))
        raw = np.fromfile(dump, dtype=np.dtype([('x', '<i4'), ('z', '<i4'), ('bits', '<u8', 5)]))
    count = np.zeros(len(raw), int); low = np.full(len(raw), 999)
    for word in range(5):
        v = raw['bits'][:, word]; count += np.array([bin(int(q)).count('1') for q in v])
    x0, z0 = raw['x'].min(), raw['z'].min(); grid = np.zeros((raw['z'].max() - z0 + 1, raw['x'].max() - x0 + 1)); grid[raw['z'] - z0, raw['x'] - x0] = count
    fig, ax = plt.subplots(figsize=(5.4, 5.4 * grid.shape[0] / grid.shape[1])); shown = np.ma.masked_equal(grid, 0)
    im = ax.imshow(shown, cmap='Blues', vmin=0, vmax=np.percentile(count, 99), extent=(x0, x0 + grid.shape[1], z0 + grid.shape[0], z0), interpolation='nearest')
    ax.set_xlabel('x'); ax.set_ylabel('z'); ax.ticklabel_format(style='plain', useOffset=False); ax.tick_params(labelsize=7.5); plt.setp(ax.get_xticklabels(), rotation=20, ha='right')
    cb = fig.colorbar(im, ax=ax, shrink=0.6, pad=0.03); cb.set_label('cavern blocks in the column', color=INK2); cb.outline.set_visible(False)
    ax.set_title(f'{count.sum():,} blocks, all in ice caves cells', fontsize=9.5)
    for s in ax.spines.values(): s.set_color(GRID)
    fig.savefig(f'{img}/{name}.png', dpi=140, bbox_inches='tight'); plt.close(fig)

if __name__ == '__main__':
    what = sys.argv[1:] or ['lens', 'phases', 'tail', 'tailnow', 'bounds', 'record']
    if 'tailnow' in what: tailnow()
    if 'bounds' in what: bounds()
    if 'lens' in what: lens()
    if 'phases' in what: phases()
    if 'tail' in what:
        fresh = [(r, 0, None) for r in ('fresh_b', 'fresh_c', 'fresh_d', 'prod1', 'prod3') if os.path.exists(f'{root}/runs/{r}/results.txt')]
        tail([('cave-gate seeds, their order', [('long1', 420, None)], RULE), ('fresh seeds, climate phases only', [('fresh_a', 0, None), ('fresh_a2', 0, None)], ORANGE),
              ('cave-gate seeds, best climate phases', [('long2', 0, None)], TEAL), ('fresh seeds, climate and cheese phases', fresh, BLUE)])
    if 'record' in what:
        best = None
        for run in os.listdir(f'{root}/runs'):
            p = f'{root}/runs/{run}/results.txt'
            if not os.path.exists(p): continue
            for l in open(p):
                f = l.split()
                if 'seconds' not in f: continue
                i = f.index('seconds')
                if 'left' in f[i + 4:i + 6] or int(f[i + 3]) != int(f[3]): continue
                if best is None or int(f[3]) > best[0]: best = (int(f[3]), f[0], f[i - 5], f[i - 4], f[i - 3])
        record(*best[1:], 'record'); print('record:', best)
