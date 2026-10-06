#!/usr/bin/env python3
"""The table of rates per box-hour of docs/PROGRESSION.md, from the counts stats/fit_exact.py leaves in
stats/fit_exact.json (run by build_pdf.sh) -> docs/rate_table.md. The first three rows are from the early fits."""
import json, os
here = os.path.dirname(os.path.abspath(__file__)); data = {d['label'].split()[0]: d for d in json.load(open(os.path.join(here, '../stats/fit_exact.json')))}
names = {'v6': 'v6 04:21 early exit; exact by F and O', 'v8': 'v8 06:06 gate on both window sizes', 'v10': 'v10 07:04 scan and first look on the GPU', 'v11': 'v11 07:28 phase windows from the GPU, leaner workers',
         'v12': 'v12 08:20 percolation term in the gate', 'v13': 'v13 10:08 cheap look on the GPU, wider filters', 'v14': 'v14 10:42 cheap looks on CPU threads (fit from 38M)'}
def cell(x): return '' if x == 0 else (f'{x:,.0f}' if x >= 10 else f'{x:.1f}')
rows = ['| caves per box-hour | 30M+ | 34M+ | 38M+ | 40M+ | 42M+ | 44M+ | a factor 10 per |', '|' + '-' * 52 + '|' + ('-' * 8 + '|') * 6 + '-' * 13 + '|',   # the dashes set the column widths
        '| v2 00:55 fast exact measure | 1.2 | 0.08 | | | | | 3.4M |', '| v3 02:05 terrain-first scanner | 11 | 1.1 | | | | | 3.4M |', '| v4 02:41 phase gate | 87 | 5.7 | | | | | 3.4M |']
for k, name in names.items():
    d = data[k]; rows.append(f'| {name} | ' + ' | '.join('' if k == 'v14' and t in ('30', '34') else cell(d['counts'][t] / d['hours']) for t in ('30', '34', '38', '40', '42', '44')) + f" | {d['decade']:.1f}M |")   # v14 does not measure most caverns under 38M
open(os.path.join(here, 'rate_table.md'), 'w').write('\n'.join(rows) + '\n')
