#!/usr/bin/env python3
"""Exact cavern volume against the formula score for the calibration sites: cal_report.py EXACT_FILE"""
import sys, numpy as np
rows = [l.split() for l in open(sys.argv[1])]
print(len(rows), 'sites done')
def col(r, i): return np.array([float(x[i]) for x in r])
for kind in ('top', 'random'):
    r = [x for x in rows if x[13] == kind]
    if not r: continue
    V, P = col(r, 3), col(r, 14); edge = np.array([x[11] == 'edge' for x in r])
    print(f'{kind}: {len(r)} sites; exact largest cavern (M blocks): median {np.median(V) / 1e6:.2f}, max {V.max() / 1e6:.2f}; formula air in the 256 window: median {np.median(P) / 1e6:.2f}; touching the dump edge: {edge.sum()}')
    for x in sorted(r, key=lambda x: -float(x[3]))[:6]: print('    V', x[3], 'core', x[4], 'x', x[5], x[6], 'y', x[7], x[8], 'z', x[9], x[10], x[11], '| P256', x[14], 'P128', x[15], 'P384', x[16])
if len(rows) > 10:
    V, P, P384 = col(rows, 3), col(rows, 14), col(rows, 16)
    print('all sites: correlation of exact V with P256', np.corrcoef(V, P)[0, 1].round(3), 'with P384', np.corrcoef(V, P384)[0, 1].round(3), '; log-log', np.corrcoef(np.log(V + 1e4), np.log(P + 1e4))[0, 1].round(3))
