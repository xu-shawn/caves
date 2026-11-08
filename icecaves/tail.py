#!/usr/bin/env python3
"""icecaves/tail.py: what the runs found, and what more compute is expected to find.

Reads runs/NAME/results.txt and seeds.log of the runs named (a cavern once: the same seed and volume from two starts is
one cavern) and prints, for the two categories
    fully ice     every block of the cavern in an ice caves cell
    ice volume    the cavern's blocks in ice caves cells, whatever the rest is
the leaders with their /tp, the counts over a few sizes per seed and per thread-hour, and an exponential fit of the
tail: sizes over a threshold u fall as exp(-(v - u) / scale), so the largest of N seeds is expected near
u + scale * ln(N * rate(u)). usage: tail.py NAME [NAME ...] [--top 10] [--plot FILE.png]"""
import math, sys

names = [a for a in sys.argv[1:] if not a.startswith('--')]; top = 10; plot = None
for i, a in enumerate(sys.argv):
    if a == '--top': top = int(sys.argv[i + 1]); names.remove(sys.argv[i + 1])
    if a == '--plot': plot = sys.argv[i + 1]; names.remove(sys.argv[i + 1])
here = __file__.rsplit('/', 1)[0]
seeds = 0; seconds = 0.0; full = {}; ice = {}; near = {}
for n in names:
    for l in open(f'{here}/runs/{n}/seeds.log'):
        f = l.split()
        if 'failed' in l: continue
        seeds += 1; seconds += float(f[2]) + float(f[4]) + float(f[6])
    for l in open(f'{here}/runs/{n}/results.txt'):
        f = l.split()
        if 'seconds' not in f: continue
        i = f.index('seconds'); volume = int(f[3]); inside = int(f[i + 3]); tp = ' '.join(f[i - 5:i - 2])
        if 'left' in f[i + 4:i + 6] or volume == 0: continue
        key = (f[0], volume)
        if inside == volume: full[key] = tp
        elif inside >= 0.999 * volume: near[key] = (tp, inside)
        ice[(f[0], inside, volume)] = tp
hours = seconds / 3600
print(f'{len(names)} runs, {seeds:,} seeds, {hours:.1f} thread-hours ({seconds / max(1, seeds):.1f} thread-seconds a seed)')

def report(title, sizes, thresholds, fit_from):
    sizes = sorted(sizes, reverse=True)
    print(f'\n{title}: {len(sizes):,} caverns measured')
    print('   ' + ';  '.join(f'{t / 1e6:g}M+: {sum(v >= t for v in sizes)} ({sum(v >= t for v in sizes) / max(hours, 1e-9):.1f} a thread-hour)' for t in thresholds))
    over = [v - fit_from for v in sizes if v >= fit_from]
    if len(over) >= 5:
        scale = sum(over) / len(over); rate = len(over) / seeds           # the exponential's maximum-likelihood scale
        print(f'   tail over {fit_from / 1e6:g}M: {len(over)} caverns, scale {scale / 1e6:.3f}M (an e-fold), so the largest expected of')
        for more in (1, 10, 100, 1000):
            n = seeds * more; print(f'      {n:>11,} seeds ({hours * more:,.0f} thread-hours): {(fit_from + scale * math.log(n * rate)) / 1e6:.2f}M')
        return sizes, scale, rate
    return sizes, None, None

fs, fscale, frate = report('Fully ice', [k[1] for k in full], (1e6, 1.5e6, 2e6, 2.5e6, 3e6), 1.5e6)
for (seed, volume), tp in sorted(full.items(), key=lambda kv: -kv[0][1])[:top]: print(f'   {volume:>10,}  seed {seed}  /tp {tp}')
if near:
    print('\nAll but under 0.1% in ice caves (the rest is in the 3-block rim of the cavern):')
    for (seed, volume), (tp, inside) in sorted(near.items(), key=lambda kv: -kv[0][1])[:max(3, top // 2)]: print(f'   {volume:>10,}  ({volume - inside:,} blocks outside)  seed {seed}  /tp {tp}')
best = {}
for (seed, inside, volume), tp in ice.items():
    if inside > best.get(seed, (0,))[0]: best[seed] = (inside, volume, tp)
isz, iscale, irate = report('Ice volume (a seed\'s best)', [v[0] for v in best.values()], (5e6, 7e6, 9e6, 11e6), 7e6)
for seed, (inside, volume, tp) in sorted(best.items(), key=lambda kv: -kv[1][0])[:top]: print(f'   {inside:>10,} of {volume:>10,} ({100 * inside / volume:.0f}%)  seed {seed}  /tp {tp}')

if plot:
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    INK, INK2, GRID, BLUE, ORANGE = '#1f2328', '#59636e', '#d8dee4', '#2a78d6', '#eb6834'
    fig, axes = plt.subplots(1, 2, figsize=(10.5, 4.2))
    for ax, sizes, scale, rate, u, color, name in ((axes[0], fs, fscale, frate, 1.5e6, BLUE, 'fully ice'), (axes[1], isz, iscale, irate, 7e6, ORANGE, 'ice volume')):
        xs = sorted(sizes); ys = [(len(xs) - k) / seeds for k in range(len(xs))]
        ax.step([x / 1e6 for x in xs], ys, where='post', color=color, lw=1.8)
        if scale:
            line = [u + scale * t / 20 for t in range(0, 200)]; ax.plot([v / 1e6 for v in line], [rate * math.exp(-(v - u) / scale) for v in line], color=INK2, lw=1, ls=(0, (4, 3)))
            ax.text(0.97, 0.93, f'tail: one e-fold per {scale / 1e6:.2f}M', transform=ax.transAxes, ha='right', va='top', fontsize=9, color=INK2)
        ax.set_yscale('log'); ax.set_ylim(0.5 / seeds, None); ax.set_xlim(xs[0] / 1e6 if len(xs) else 0, None)
        ax.set_xlabel(f'{name}, million blocks', color=INK2); ax.set_ylabel('caverns of at least that size per seed', color=INK2)
        ax.grid(True, which='major', color=GRID, lw=0.6); ax.set_axisbelow(True); ax.tick_params(colors=INK2, labelsize=8.5)
        for s in ('top', 'right'): ax.spines[s].set_visible(False)
        for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
    fig.suptitle(f'Ice caverns found in {seeds:,} seeds ({hours:.0f} thread-hours): how many per seed reach a size', x=0.01, ha='left', fontsize=11, color=INK)
    fig.tight_layout(rect=(0, 0, 1, 0.95)); fig.savefig(plot, dpi=130); print('\nplot ->', plot)
