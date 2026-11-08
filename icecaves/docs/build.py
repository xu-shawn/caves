#!/usr/bin/env python3
"""icecaves/docs/build.py: ICECAVES.md with its tables filled from runs/ -> ICECAVES_built.md -> ICECAVES.pdf (md2pdf.sh).
The two paragraphs that are conclusions (FRESH_TEXT, LIMITS_TEXT) are in docs/conclusions.txt, separated by a line "----".
usage: docs/build.py"""
import os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here); sys.path.insert(0, here)
from plots import caverns

def source(runs):
    n = 0; t = 0; v = []
    for run, first, last in runs: k, s, vv, _, _ = caverns(run, first, last); n += k; t += s; v += vv
    over = [x - 2e6 for x in v if x >= 2e6]
    return n, t, v, (sum(over) / len(over) if len(over) >= 5 else float('nan'))
SOURCES = [('cave-gate seeds, their own order', [('long1', 420, None)], 0),          # (name, runs, share of seeds the gate keeps: 0 = no scan of our own)
           ('cave-gate seeds, best climate phases', [('long2', 0, None)], 0),
           ('fresh, climate phases only', [('fresh_a', 0, None), ('fresh_a2', 0, None)], 2e-7),
           ('fresh, climate tight + cheese loose', [('fresh_b', 0, None)], 4.6e-9),
           ('fresh, ten windows by weight', [('fresh_c', 0, None)], 5e-8),
           ('fresh, twelve windows (with pillars)', [('fresh_d', 0, None)], 2e-8),
           ('fresh, ten windows, refit centres (production gate)', [('prod1', 0, None)], 2e-8),
           ('fresh, fourteen windows (entrance and pillar layers added)', [('prod2', 0, None)], 2e-8),
           ('production gate, half the pairs by own weirdness', [('prod3', 680, None)], 2e-8)]   # (the runs after the weirdness tables are in NOW_TABLE)
SCAN = 3e7   # seeds a second the gate looks at on one busy laptop thread
rows = ['| seeds | scanned | thread-s a seed (gate + pipeline) | 2M+ per seed | 2.5M+ | 3M+ | 2M+ per thread-hour | 2.5M+ | 3M+ |', '|:--------------------------|---:|---:|---:|---:|---:|---:|---:|---:|']
for name, runs, share in SOURCES:
    if not all(os.path.exists(f'{root}/runs/{r[0]}/seeds.log') for r in runs): continue
    n, t, v, scale = source(runs)
    if n < 50: continue
    gate = n / (SCAN * share) if share else 0; c = [sum(x >= thr for x in v) for thr in (2e6, 2.5e6, 3e6)]
    rows.append(f'| {name} | {n:,} | {gate / n:.1f} + {t / n:.0f} | ' + ' | '.join(f'{k / n:.3f}' for k in c) + ' | ' + ' | '.join(f'{3600 * k / (t + gate):.1f}' for k in c) + ' |')
gate_table = '\n'.join(rows) + '\n\n(Caverns that are ice caves throughout; one measuring policy for all rows.)'

results = subprocess.run([sys.executable, f'{root}/results.py', '--top', '12'], capture_output=True, text=True).stdout
open(f'{root}/RESULTS.md', 'w').write(results)
tables = results.split('\n', 2)[2].replace('\n## ', '\n### ')   # without the title; one level down
best = ['0', '', '']
for line in results.splitlines():
    if line.startswith('| ') and line[2].isdigit(): f = [x.strip() for x in line.strip('|').split('|')]; best = f; break
fresh, limits = (open(f'{here}/conclusions.txt').read().split('\n----\n') + ['', ''])[:2]
# the recall table: every setting in runs/recall/summary.txt that docs/recall_names.txt names ("setting | description")
recall_rows = ['| scanner setting | 2.7M+ kept | 3M+ kept | 3.5M+ kept | scanner, s a seed (busy machine) |', '|:---|---:|---:|---:|---:|']
labels = dict((l.split('|')[0].strip(), l.split('|', 1)[1].strip()) for l in open(f'{here}/recall_names.txt') if '|' in l) if os.path.exists(f'{here}/recall_names.txt') else {}
import re
done_rows = {}
if os.path.exists(f'{root}/runs/recall/summary.txt'):
    for l in open(f'{root}/runs/recall/summary.txt'):
        m = re.match(r'(\S+): kept (\d+) of (\d+) caverns of 2.7M\+, (\d+) of (\d+) of 3M\+, (\d+) of (\d+) of 3.5M\+; scanner ([0-9.]+) s', l)
        if m: done_rows[m.group(1)] = m.groups()
for name, text in labels.items():
    if name in done_rows: g = done_rows[name]; recall_rows.append(f'| {text} | {g[1]} of {g[2]} | {g[3]} of {g[4]} | {g[5]} of {g[6]} | {float(g[7]):.0f} |')
recall_table = '\n'.join(recall_rows)
# the paired tests: the whole pipeline on the same seeds (runs/pairtest.txt: laptop, 200 seeds; ptest.txt: the box, 651)
def test_lines(path):
    out = {}
    if os.path.exists(path):
        for l in open(path):
            m = re.match(r'(\S+): (\d+) seeds, ([0-9.]+) thread-s a seed.*?; (\d+) of 2M\+, (\d+) of 2.5M\+, (\d+) of 3M\+, (\d+) of 3.5M\+', l)
            if m: out[m.group(1)] = m.groups()
    return out
lap = test_lines(f'{root}/runs/pairtest.txt'); box = test_lines(f'{root}/ptest.txt')
PAIRED = [(lap, 'pt_base', 'laptop, 200 seeds: 1M pairs, half by their own weirdness, windows of 12 and 16 (before)'), (lap, 'pt_w35_16m', '16M pairs, box weirdness 0.35+'), (lap, 'pt2_np96', 'the same, 96 formula candidates by size'),
          (lap, 'pt3_s12_fi48', 'windows of 12 only, 48 candidates by the index'), (lap, 'pt3_s10_fi48', 'windows of 10'), (lap, 'pt4_s8', 'windows of 8'),
          (box, 'b_s10', 'box, 651 seeds: windows of 10'), (box, 'b_s8', 'windows of 8'), (box, 'b_s6', 'windows of 6'), (box, 'b_s4', 'windows of 4'), (box, 'b_s6_32m_w40', 'windows of 6, 32M pairs, box weirdness 0.40+ (in use)'), (box, 'b_s6_p2000', 'windows of 6, 2,000 fine looks, 64 candidates')]
paired_rows = ['| setting | thread-s a seed | 2M+ | 2.5M+ | 3M+ | 3.5M+ |', '|:---|---:|---:|---:|---:|---:|']
for src, key, text in PAIRED:
    if key in src: g = src[key]; paired_rows.append(f'| {text} | {float(g[2]):.1f} | ' + ' | '.join(f'{int(x):,}' for x in g[3:7]) + ' |')
paired_table = '\n'.join(paired_rows) + '\n\n(Caverns that are ice caves throughout; the exact test from 350 samples in every row.)'
# before and now, by policy (a run is cut where its notes.txt say the policy changed)
import math
from plots import POLICIES
PERIODS = [(name, parts, least < 4.5e6) for name, parts, least, color in POLICIES]
now_rows = ['| policy | seeds | 4M+ per 1,000 seeds | 4.5M+ | 5M+ | 5.5M+ | e-fold above 4M |', '|:---|---:|---:|---:|---:|---:|---:|']; tail_now = 'n/a'
for name, parts, whole in PERIODS:
    n = 0; v = []
    for run, first, last in parts:
        if os.path.exists(f'{root}/runs/{run}/results.txt') and os.path.exists(f'{root}/runs/{run}/seeds.log'): k, s, vv, _, _ = caverns(run, first, last); n += k; v += vv
    if n < 50: continue
    c = [1000 * sum(x >= t for x in v) / n for t in (4e6, 4.5e6, 5e6, 5.5e6)]; ex = [x - 4e6 for x in v if x >= 4e6]
    fold = f'{sum(ex) / len(ex) / 1e6:.2f}M +- {sum(ex) / len(ex) / 1e6 / math.sqrt(len(ex)):.2f}' if whole and len(ex) >= 8 else ''
    now_rows.append(f'| {name} | {n:,} | ' + (f'{c[0]:.1f}' if whole else '') + ' | ' + (f'{c[1]:.2f}' if whole else '') + f' | {c[2]:.2f} | {c[3]:.2f} | {fold} |')
    if 'nearly' in name and len(ex) >= 8: tail_now = f'{sum(ex) / len(ex) / 1e6:.2f} +- {sum(ex) / len(ex) / 1e6 / math.sqrt(len(ex)):.2f}'
now_table = '\n'.join(now_rows) + '\n\n(Caverns that are ice caves throughout; the counts of the last row are complete from 4.7M only. On the box a seed takes 11 thread-seconds with the fuller policies and 7 with the largest-only one: 10,000 and 16,000 seeds an hour for $0.14.)'
gatefit_note = open(f'{here}/gatefit_note.txt').read().strip() if os.path.exists(f'{here}/gatefit_note.txt') else 'largest: 0.12 +- 0.05'
md = open(f'{here}/ICECAVES.md').read()
for key, value in (('RECORD_M', f"{int(best[0].replace(',', '')) / 1e6:.2f}"), ('RECORD_SEED', best[1]), ('RECORD_TP', best[2]), ('RECORD_BLOCKS', best[0]), ('GATE_TABLE', gate_table), ('PAIRED_TABLE', paired_table), ('NOW_TABLE', now_table), ('RECALL_TABLE', recall_table), ('FRESH_TEXT', fresh.strip()), ('RESULTS_TABLES', tables.strip()), ('LIMITS_TEXT', limits.strip()), ('TAIL_NOW', tail_now), ('GATEFIT_NOTE', gatefit_note)):
    md = md.replace(key, value)
open(f'{here}/ICECAVES_built.md', 'w').write(md)
subprocess.run(['bash', f'{here}/md2pdf.sh', 'ICECAVES_built'], check=True)
os.replace(f'{here}/ICECAVES_built.pdf', f'{here}/ICECAVES.pdf'); print('wrote', f'{here}/ICECAVES.pdf')
