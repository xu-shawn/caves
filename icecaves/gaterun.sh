#!/bin/bash
# icecaves/gaterun.sh: the search on fresh seeds: the phase gate (icephase gate) feeding seed.sh, in chunks.
# usage: gaterun.sh NAME SPEC_FILE [SCAN_THREADS=2] [JOBS=10] [COUNT=40000000000] [FIRST=1000000000000]
#   SPEC_FILE: one line, the layers of the gate (see icephase.cpp, "gate"); COUNT: seeds looked at per chunk (at 1.5e-8
#   kept that is 600 seeds); FIRST: the index the first chunk starts at (the gate walks a mix of the index).
#   While JOBS workers scan one chunk's seeds, SCAN_THREADS look for the next chunk's. Results in runs/NAME as for
#   longrun.sh (seeds.log, results.txt, progress.txt, and gate.txt: every seed the gate kept, with its phases);
#   runs/NAME/next_index holds where the next chunk starts, so a run resumes; `touch runs/NAME/STOP` ends it after the
#   chunk that is being scanned, and it ends by itself after MAX_CHUNKS chunks (env, default 12: about 7,000 seeds).
D="$(cd "$(dirname "$0")" && pwd)"; N=$1; SPEC="$(cat "$2")"; ST=${3:-2}; J=${4:-10}; C=${5:-40000000000}; O="$D/runs/$N"; mkdir -p "$O"
touch "$O/seeds.log"; [ -f "$O/next_index" ] || echo ${6:-1000000000000} > "$O/next_index"
cp "$2" "$O/spec.txt"; t0=$(date +%s)
# (GATE_GPU=1: the same gate on a CUDA card, icegate; SCAN_THREADS is then not used)
look() { local at=$(cat "$O/next_index"); if [ -n "$GATE_GPU" ]; then "$D/icegate" $at $C "$SPEC" > "$O/chunk_next.txt" 2>> "$O/gate.log"; else "$D/icephase" gate $at $C $ST "$SPEC" > "$O/chunk_next.txt" 2>> "$O/gate.log"; fi; echo $((at + C)) > "$O/next_index"; }
[ -s "$O/chunk.txt" ] || { look; mv -f "$O/chunk_next.txt" "$O/chunk.txt"; }
chunks=0
while [ ! -f "$O/STOP" ] && [ $chunks -lt ${MAX_CHUNKS:-12} ]; do
    chunks=$((chunks + 1))
    look & scanner=$!
    cat "$O/chunk.txt" >> "$O/gate.txt"
    awk 'FILENAME == ARGV[1] {done[$1]; next} !($1 in done) {print $1}' "$O/seeds.log" "$O/chunk.txt" 2>/dev/null | KEEP_FORMULA=0 xargs -P $J -I{} "$D/seed.sh" {} "$O"
    wait $scanner; mv -f "$O/chunk_next.txt" "$O/chunk.txt"
    echo "$(wc -l < "$O/seeds.log") seeds, $(( $(date +%s) - t0 )) s this session, next index $(cat "$O/next_index")" >> "$O/progress.txt"
done
