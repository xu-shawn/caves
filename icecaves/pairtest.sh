#!/bin/bash
# icecaves/pairtest.sh: two settings on the same seeds, by what the whole pipeline finds and what it costs.
# usage: pairtest.sh NAME SEEDS_FILE JOBS [VAR=value ...]     (the VARs go to seed.sh's environment; SCANNER=icepairs7 for another build)
# Results in runs/NAME as for any run; prints seeds, thread-seconds a seed (scanner + formula + measure) and the caverns
# that are ice caves throughout of 2M+, 2.5M+, 3M+ and 3.5M+.
D="$(cd "$(dirname "$0")" && pwd)"; N=$1; F=$2; J=$3; shift 3; O="$D/runs/$N"; mkdir -p "$O"; touch "$O/seeds.log"
awk 'FILENAME == ARGV[1] {done[$1]; next} !($1 in done) {print $1}' "$O/seeds.log" "$F" | env KEEP_FORMULA=0 "$@" xargs -P $J -I{} "$D/seed.sh" {} "$O"
python3 - "$O" "$N" <<'PY'
import sys
O, N = sys.argv[1:3]; n = 0; t = [0, 0, 0]
for l in open(f'{O}/seeds.log'):
    f = l.split()
    if len(f) >= 30 and f[15] == 'fully_ice_and_most_ice': n += 1; t[0] += float(f[2]); t[1] += float(f[4]); t[2] += float(f[6])
v = set()
for l in open(f'{O}/results.txt'):
    f = l.split()
    if 'seconds' not in f: continue
    i = f.index('seconds')
    if 'left' in f[i + 4:i + 6] or int(f[3]) == 0 or int(f[i + 3]) != int(f[3]): continue
    v.add((f[0], int(f[3])))
sizes = [x[1] for x in v]
print(f'{N}: {n} seeds, {sum(t) / max(n, 1):.1f} thread-s a seed (scanner {t[0] / max(n, 1):.1f}, formula {t[1] / max(n, 1):.1f}, measure {t[2] / max(n, 1):.1f}); ' + ', '.join(f'{sum(1 for x in sizes if x >= s)} of {s / 1e6:g}M+' for s in (2e6, 2.5e6, 3e6, 3.5e6)) + f'; largest {max(sizes, default=0):,}')
PY
