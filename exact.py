#!/usr/bin/env python3
"""Exact cavern volumes for a list of sites: the real game's noise terrain around each site (java/CaveDump, one JVM for
the whole list) and cavemetric on it.
usage: exact.py [SIZE=768] [R=3] [THREADS=11] < lines "SEED X Z [anything]"  ->  "SEED X Z VOLUME CORE x0 x1 y0 y1 z0 z1 edge|ok | rest of the input line"
The site is the centre of a SIZE x SIZE region (moved to whole chunks). The line is the largest cavern of the region."""
import os, subprocess, sys, tempfile
size = int(sys.argv[1]) if len(sys.argv) > 1 else 768; R = int(sys.argv[2]) if len(sys.argv) > 2 else 3; threads = sys.argv[3] if len(sys.argv) > 3 else '11'
here = os.path.dirname(os.path.abspath(__file__))
java = os.environ.get('JAVA25', os.path.expanduser('~/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java'))
cp = here + '/java/out:' + open(here + '/java/classpath.txt').read().strip().replace('mc/', here + '/mc/')
tmp = os.environ.get('CAVE_TMP', tempfile.gettempdir()); out = os.path.join(tmp, f'cavedump_{os.getpid()}.bin')
proc = subprocess.Popen([java, '-Xmx6g', '-cp', cp, 'CaveDump', '--batch', threads], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
for line in sys.stdin:
    w = line.split()
    if len(w) < 3: continue
    seed, x, z = w[0], int(float(w[1])), int(float(w[2])); x0 = ((x - size // 2) >> 4) << 4; z0 = ((z - size // 2) >> 4) << 4
    proc.stdin.write(f'{seed} {x0} {z0} {size} {size} {out}\n'); proc.stdin.flush()
    while True:
        reply = proc.stdout.readline()
        if not reply: sys.exit('CaveDump stopped')
        if reply.startswith('done'): break
    res = subprocess.run([here + '/cavemetric', out, str(x0), str(z0), str(size), str(size), str(R), '1'], capture_output=True, text=True).stdout.splitlines()
    top = res[1].split() if len(res) > 1 else ['0', '0', 'x', '0..0', 'y', '0..0', 'z', '0..0']
    xr, yr, zr = (top[i].replace('..', ' ') for i in (3, 5, 7))
    print(seed, x, z, top[0], top[1], xr, yr, zr, 'edge' if top[-1] == 'edge' else 'ok', '|', ' '.join(w[3:]), flush=True)
proc.stdin.close(); proc.wait()
if os.path.exists(out): os.remove(out)
