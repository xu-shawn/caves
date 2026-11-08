#!/bin/bash
# icecaves/toprecall.sh: do settings still find the largest caverns that are known?
# runs/top_old.txt has "SEED X Z VOLUME" of the caverns of 3.6M+ found before the weirdness tables; the whole pipeline
# (seed.sh, exact test from 450 samples) is run on their seeds under a setting, and a cavern counts as found when the
# run measures a cavern that is ice caves throughout of the same volume in that seed.
# usage: toprecall.sh NAME JOBS [VAR=value ...]
# LIST=name: another list, runs/name.txt (top_new: the caverns of 4.7M+ of the runs with the new scanner).
D="$(cd "$(dirname "$0")" && pwd)"; N=$1; J=$2; shift 2; O="$D/runs/$N"; mkdir -p "$O"; touch "$O/seeds.log"; L="$D/runs/${LIST:-top_old}.txt"
cut -d' ' -f1 "$L" | sort -u | awk 'FILENAME == ARGV[1] {done[$1]; next} !($1 in done) {print $1}' "$O/seeds.log" - | env KEEP_FORMULA=0 "$@" xargs -P $J -I{} "$D/seed.sh" {} "$O"
python3 - "$D" "$O" "$N" "$L" <<'PY'
import sys
D, O, N, L = sys.argv[1:5]; got = set()
for l in open(f'{O}/results.txt'):
    f = l.split()
    if 'seconds' not in f: continue
    i = f.index('seconds')
    if 'left' in f[i + 4:i + 6] or int(f[i + 3]) != int(f[3]): continue
    got.add((f[0], int(f[3])))
known = [(l.split()[0], int(l.split()[3])) for l in open(L)]
new = sorted((v for s, v in got if (s, v) not in set(known) and v >= 3.6e6), reverse=True)
t = [sum(float(x) for x in (l.split()[2], l.split()[4], l.split()[6])) for l in open(f'{O}/seeds.log') if len(l.split()) >= 30]
print(f'{N}: ' + ', '.join(f'{sum(1 for k in known if k[1] >= th and k in got)} of {sum(1 for k in known if k[1] >= th)} of {th / 1e6:g}M+' for th in (3.6e6, 4.3e6, 4.7e6, 5e6)) + f' known caverns found; {len(new)} other caverns of 3.6M+ in those seeds (largest {new[0] if new else 0:,}); {sum(t) / max(len(t), 1):.1f} thread-s a seed')
PY
