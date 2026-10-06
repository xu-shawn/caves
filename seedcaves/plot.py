#!/usr/bin/env python3
"""seedcaves: the two frontiers of caverns.csv as one chart (frontier.png): the largest cavern within a distance of the
origin, by total volume and by volume outside the deep dark; the other measured caverns as faint context.
usage: plot.py [OUT=frontier.png]      (colours: slots 1 and 2 of the validated categorical palette, blue and orange)"""
import csv, os, sys, math, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
from matplotlib.transforms import offset_copy
here = os.environ.get('WORK', os.path.dirname(os.path.abspath(__file__))); out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, 'frontier.png'); SEED = os.environ.get('SEED', '-5296147297896900941')
SURF, INK, INK2, MUTED, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#898781', '#e1e0d9'; BLUE, ORANGE, AQUA = '#2a78d6', '#eb6834', '#1baf7a'
rows = list(csv.DictReader(open(os.path.join(here, 'caverns.csv'))))
for r in rows:
    for k in ('distance', 'volume', 'free_volume', 'strict_free_volume'): r[k] = float(r[k])
EDGE = 4.24e7   # the far corner of the world
fig, ax = plt.subplots(figsize=(11, 6.6), dpi=150); fig.patch.set_facecolor(SURF); ax.set_facecolor(SURF); ax.set_axisbelow(True); ax.tick_params(colors=MUTED, length=0); ax.grid(True, color=GRID, lw=0.8)
for s in ('top', 'right'): ax.spines[s].set_visible(False)
for s in ('left', 'bottom'): ax.spines[s].set_color(GRID)
# (series: value, frontier flag, colour, name, side of the labels, the line's shift in points: where two frontiers are the
# same caverns their lines would hide each other, so the second and third run just under the first)
series = [('volume', 'frontier_total', BLUE, 'total volume', 1, 0.0), ('free_volume', 'frontier_free', ORANGE, 'volume outside the deep dark', -1, -2.2)]
if not os.environ.get('NO_STRICT'): series.append(('strict_free_volume', 'frontier_strict', AQUA, 'caverns with no deep dark at all', -1, -4.4))
# context: every other measured cavern at its total volume, faint
pts = [r for r in rows if r['frontier_total'] != '1']
ax.scatter([r['distance'] for r in pts], [r['volume'] / 1e6 for r in pts], s=8, color=MUTED, alpha=0.20, linewidths=0, zorder=2)
labelled = []   # (log x, y) of the labels placed, to keep them apart
top = max(r['volume'] for r in rows) / 1e6
shown = {}   # cavern -> the value already written next to it
for key, flag, colour, name, side, shift in series:
    lower = offset_copy(ax.transData, fig=fig, y=shift, units='points')
    front = sorted((r for r in rows if r[flag] == '1'), key=lambda r: r['distance'])
    if not front: continue
    xs = [r['distance'] for r in front] + [EDGE]; ys = [r[key] / 1e6 for r in front]; ys = ys + [ys[-1]]
    ax.step(xs, ys, where='post', color=colour, lw=2, zorder=3.5 - 0.1 * len(shown and [1]), label=name, transform=lower, ls='-' if key != 'strict_free_volume' else (0, (5, 2)))
    ax.scatter(xs[:-1], ys[:-1], s=42, color=colour, edgecolors=SURF, linewidths=1.6, zorder=4, transform=lower)
    for r in front:   # the value at each frontier point, where there is room
        lx, y = math.log10(r['distance']), r[key] / 1e6; ty = y + side * 0.035 * top; text = f'{y:.1f}M' if y >= 0.95 else f'{y * 1000:.0f}K'
        if shown.get((r['tp_x'], r['tp_z'])) == text: continue   # the same cavern with the same value on an earlier frontier
        if key == 'strict_free_volume' and r is not front[-1]: continue   # (the third curve: its end only)
        if any(abs(lx - px) < 0.19 and abs(ty - py) < 0.05 * top for px, py in labelled): continue
        labelled.append((lx, ty)); shown[(r['tp_x'], r['tp_z'])] = text
        ax.annotate(text, (r['distance'], y), xytext=(-4 if side > 0 else 5, 7 * side + shift), textcoords='offset points', ha='right' if side > 0 else 'left', va='bottom' if side > 0 else 'top', fontsize=8.5, color=INK2, zorder=5)
ax.set_xscale('log'); ax.set_xlim(min(r['distance'] for r in rows) * 0.7, EDGE * 1.25); ax.set_ylim(0, top * 1.12)
ticks = [t for t in (100, 1e3, 1e4, 1e5, 1e6, 1e7) if ax.get_xlim()[0] <= t <= EDGE]; ax.set_xticks(ticks); ax.set_xticklabels([{100: '100', 1e3: '1,000', 1e4: '10,000', 1e5: '100,000', 1e6: '1 million', 1e7: '10 million'}[t] for t in ticks]); ax.minorticks_off()
ax.axvline(2.999e7, color=MUTED, lw=1, ls=(0, (3, 3)), zorder=1); ax.text(2.999e7 * 0.95, top * 0.02, 'world border along an axis', color=INK2, fontsize=8.5, ha='right', va='bottom', rotation=90, bbox=dict(facecolor=SURF, edgecolor='none', alpha=0.85, pad=1.5), zorder=2.5)
ax.set_xlabel('distance of the cavern from (0, 0), blocks (its /tp point)', color=INK2); ax.set_ylabel('cavern volume, million blocks', color=INK2)
fig.suptitle('The largest cavern within a distance of the origin', x=0.012, y=0.94, ha='left', fontsize=14, color=INK, fontweight='bold')
others = len(rows) - sum(r['frontier_total'] == '1' for r in rows)
ax.set_title(f'Seed {SEED}, large biomes, Minecraft 26.3. Each marked point is a cavern larger (by that measure) than every measured cavern\ncloser to (0, 0); a line holds its value until the next one. Grey dots: the other {others:,} measured caverns, at their total volume.',
             loc='left', fontsize=9, color=INK2, pad=10, linespacing=1.4)
leg = ax.legend(loc='upper left', frameon=False, fontsize=10); [t.set_color(INK2) for t in leg.get_texts()]
fig.tight_layout(rect=(0, 0, 1, 0.93)); fig.savefig(out, facecolor=SURF); print('wrote', out)
