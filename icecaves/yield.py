#!/usr/bin/env python3
"""What a run of seeds gave for its compute: per seed the time of each stage, and the caverns per thread-hour.
usage: yield.py RUN_DIR [WALL_SECONDS JOBS]"""
import sys
d = sys.argv[1]; rows = [l.split() for l in open(d + '/seeds.log') if 'failed' not in l]
rows = [r for r in rows if len(r) >= 20 and r[15] == 'fully_ice_and_most_ice' and r[18] == 'strict']   # (a line with a field missing is left out)
n = len(rows); t = [sum(float(r[k]) for r in rows) / n for k in (2, 4, 6)]; full = sorted((int(r[16]) for r in rows), reverse=True); ice = sorted((int(r[17]) for r in rows), reverse=True)
cpu = float(sys.argv[2]) * int(sys.argv[3]) if len(sys.argv) > 3 else sum(t) * n
strict = sum(float(r[19]) for r in rows if len(r) > 19) / n
print(f'{n} seeds; a seed: scanner {t[0]:.1f} s, formula {t[1]:.1f} s, measure {t[2]:.1f} s (strict tests {strict:.1f} s); {cpu / n:.0f} thread-seconds a seed in all ({3600 * n / cpu:.0f} seeds a thread-hour)')
print(f'  fully ice: best {full[0]:,}; mean of the seeds\' best {sum(full) / n:,.0f}; seeds with 1M+ {sum(v >= 1e6 for v in full)}, 1.5M+ {sum(v >= 1.5e6 for v in full)}, 2M+ {sum(v >= 2e6 for v in full)}  ->  per thread-hour: 1M+ {3600 * sum(v >= 1e6 for v in full) / cpu:.1f}, 1.5M+ {3600 * sum(v >= 1.5e6 for v in full) / cpu:.2f}')
print(f'  ice volume: best {ice[0]:,}; mean {sum(ice) / n:,.0f}; seeds with 5M+ {sum(v >= 5e6 for v in ice)}, 8M+ {sum(v >= 8e6 for v in ice)}  ->  per thread-hour: 5M+ {3600 * sum(v >= 5e6 for v in ice) / cpu:.1f}, 8M+ {3600 * sum(v >= 8e6 for v in ice) / cpu:.2f}')
