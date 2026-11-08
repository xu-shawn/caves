#!/bin/bash
# icecaves/longrun.sh: seed.sh over a long list of seeds, JOBS at a time, in chunks; resumes where it stopped.
# usage: longrun.sh NAME SEEDS_FILE [JOBS=12] [CHUNK=600]
#   results in runs/NAME (seeds.log, results.txt); `touch runs/NAME/STOP` ends it after the chunk that is running;
#   runs/NAME/progress.txt gets a line per chunk: seeds done, seconds, the best so far in both categories.
D="$(cd "$(dirname "$0")" && pwd)"; N=$1; F=$2; J=${3:-12}; C=${4:-600}; O="$D/runs/$N"; mkdir -p "$O"; touch "$O/seeds.log"
awk 'FILENAME == ARGV[1] {done[$1]; next} !($1 in done) {print $1}' "$O/seeds.log" "$F" > "$O/todo.txt"
t0=$(date +%s); from=1; total=$(wc -l < "$O/todo.txt")
while [ $from -le $total ] && [ ! -f "$O/STOP" ]; do
    sed -n "${from},$((from + C - 1))p" "$O/todo.txt" | KEEP_FORMULA=0 xargs -P $J -I{} "$D/seed.sh" {} "$O"
    from=$((from + C))
    echo "$(wc -l < "$O/seeds.log") seeds, $(( $(date +%s) - t0 )) s this session; best fully ice $(awk '{if ($17 > b) b = $17} END {print b + 0}' "$O/seeds.log"), most ice $(awk '{if ($18 > b) b = $18} END {print b + 0}' "$O/seeds.log")" >> "$O/progress.txt"
done
