#!/bin/bash
# Everything for one seed (large biomes, Minecraft 26.3): its biggest caverns by distance from the origin, by total volume
# and by volume outside the deep dark. All the work is native (ringpairs, formulacave_lb, ddmeasure); the scripts are glue.
#   1. rings.sh      the scanner per ring of distances (2,000 blocks to the border, a factor 2 a ring)   -> WORK/ring_*.txt
#   2. stage2.py     the formula stage on every site, and the exact measure of a few per band: a first frontier
#   3. stage3.py     the sites that could still be on a frontier (exact <= 1.75 x the formula cluster, 1.9 x for the
#                    volume outside the deep dark): the BATCH most promising measured, then again with the frontier they
#                    raise, until none are left
#   4. frontier.py, plot.py     WORK/caverns.csv, frontier.txt, frontier.png
# usage: run_seed.sh SEED [WORK=seeds/SEED]      env THREADS=6, TOP=600 (scanner sites a ring, by cluster and as many by free), K=6, BATCH= (e.g. 150: best-first rounds; a quarter fewer measurements, but no faster by the clock)
D="$(cd "$(dirname "$0")" && pwd)"; export SEED=$1 THREADS=${THREADS:-6}; export WORK=${2:-$D/seeds/$1}; mkdir -p "$WORK"; t0=$(date +%s)
stamp() { echo "$(( $(date +%s) - t0 )) s: $*"; }
TOP=${TOP:-600} PLACES=${PLACES:-16000} OUTER_PAIRS=${OUTER_PAIRS:-3000000} OUTER_PLACES=${OUTER_PLACES:-80000} PREFIX="$WORK/ring" "$D/rings.sh" > "$WORK/rings.out" 2>&1
stamp "rings scanned: $(cat "$WORK"/ring_*.txt | wc -l) sites"
K_TOTAL=${K:-6} K_FREE=${K:-6} python3 "$D/stage2.py" $THREADS; stamp "formula stage and the first measurements"
n=3; while :; do
    left=$(SUFFIX=$n BATCH=${BATCH:-} python3 "$D/stage3.py" $THREADS | tee -a "$WORK/stage3.out" | awk '/after spacing/ {print $(NF - 2)}')
    [ "${left:-0}" -eq 0 ] && break; n=$((n + 1)); [ $n -gt 200 ] && break
done
stamp "exact measurements: $(cat "$WORK"/measured_raw*.txt | wc -l) starts in $((n - 2)) rounds"
python3 "$D/frontier.py" > "$WORK/frontier.txt"; python3 "$D/plot.py" > /dev/null; stamp "done: $WORK/caverns.csv, frontier.txt, frontier.png"
