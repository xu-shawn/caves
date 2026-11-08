#!/bin/bash
# icecaves/recall.sh: how many known large caverns the scanner still lists under a setting.
# usage: recall.sh NAME JOBS [PAIRS=1000000] [PLACES=900] [VAR=value ...]     (BIN=icepairs2 for another build; SEEDS=file for
#        other seeds than the known caverns': then distinct.py NAME counts what the scanner lists on them)
# runs/recall_caverns.txt has "SEED X Z VOLUME" of caverns that are ice caves throughout (from the runs' results);
# the scanner alone is run on their seeds (runs/recall_seeds.txt) and a cavern counts as kept when a listed place within
# 320 blocks of it has a fine score (pure cluster, or ice cluster with at most 8% outside) of 220+, which sends it on
# to the formula. Prints kept / known for 2.7M+, 3M+ and 3.5M+, and the scanner's time.
D="$(cd "$(dirname "$0")" && pwd)"; N=$1; J=$2; P=${3:-1000000}; L=${4:-900}; shift 4 2>/dev/null; O="$D/runs/recall/$N"; mkdir -p "$O"
one() { t0=$(date +%s.%N); env "${@:4}" "$D/${BIN:-icepairs}" $1 $2 100 1 $3 2>/dev/null > "$O/$1.txt"; echo "$1 $(echo "$(date +%s.%N) - $t0" | bc)" >> "$O/times.txt"; }
export -f one; export D O BIN; : > "$O/times.txt"
xargs -P $J -I{} bash -c 'one {} '"$P $L $*" < "${SEEDS:-$D/runs/recall_seeds.txt}"
python3 - "$D" "$O" "$N" <<'PY'
import sys, os
D, O, N = sys.argv[1:4]; kept = [0, 0, 0]; known = [0, 0, 0]
for l in open(f'{D}/runs/recall_caverns.txt'):
    seed, x, z, v = l.split(); x = int(x); z = int(z); v = int(v); found = False
    path = f'{O}/{seed}.txt'
    if os.path.exists(path):
        for q in open(path):
            f = q.split(); ice, total, pure = int(f[4]), int(f[6]), int(f[8]); score = max(pure, ice if (total - ice) * 100 <= 8 * total else 0)
            if score < 220: continue
            for px, pz in ((float(f[19]), float(f[20])), (float(f[16]), float(f[17]))):
                if abs(px - x) <= 320 and abs(pz - z) <= 320: found = True
    for k, t in enumerate((2.7e6, 3e6, 3.5e6)):
        if v >= t: known[k] += 1; kept[k] += found
times = [float(l.split()[1]) for l in open(f'{O}/times.txt')]
print(f'{N}: kept {kept[0]} of {known[0]} caverns of 2.7M+, {kept[1]} of {known[1]} of 3M+, {kept[2]} of {known[2]} of 3.5M+; scanner {sum(times) / max(len(times), 1):.1f} s a seed')
PY
