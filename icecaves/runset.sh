#!/bin/bash
# icecaves/runset.sh: seed.sh over a file of seeds, JOBS at a time, and what came of it per second of one thread.
# usage: runset.sh NAME [JOBS=12]      (seeds in runs/NAME/seeds.txt; env as for seed.sh)
D="$(cd "$(dirname "$0")" && pwd)"; N=$1; J=${2:-12}; t0=$(date +%s)
xargs -P $J -I{} "$D/seed.sh" {} "$D/runs/$N" < "$D/runs/$N/seeds.txt"
python3 "$D/yield.py" "$D/runs/$N" $(( $(date +%s) - t0 )) $J
