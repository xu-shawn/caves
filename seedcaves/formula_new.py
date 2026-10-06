#!/usr/bin/env python3
"""seedcaves: the formula stage on the scanner sites (ring*_*.txt) that have no formula line yet -> formula_all<SUFFIX>.txt
usage: SUFFIX=3 formula_new.py [THREADS=6]"""
import subprocess, sys, os, glob
here = os.path.dirname(os.path.abspath(__file__)); os.chdir(os.environ.get('WORK', here)); threads = sys.argv[1] if len(sys.argv) > 1 else '6'; SUFFIX = os.environ['SUFFIX']
have = set()
for f in glob.glob('formula_all*.txt'):
    if f != f'formula_all{SUFFIX}.txt': have |= {tuple(l.split()[1:3]) for l in open(f) if l.strip()}
sites, seen = [], set()
for f in sorted(glob.glob('ring*_*.txt')):
    for l in open(f):
        w = l.split()
        if len(w) > 3 and (w[1], w[2]) not in have and (w[1], w[2]) not in seen: seen.add((w[1], w[2])); sites.append(l.strip() + f' src {f[:-4]}')
out = subprocess.run([os.path.join(here, 'formulacave_lb'), threads, '1536', '48'], input='\n'.join(sites) + '\n', capture_output=True, text=True).stdout
open(f'formula_all{SUFFIX}.txt', 'w').write(out); print(f'{len(sites)} new sites through the formula stage')
