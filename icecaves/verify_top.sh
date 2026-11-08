#!/bin/bash
# icecaves/verify_top.sh: the game's biome source for the largest caverns that are not yet in checked.txt.
# usage: verify_top.sh [N=8]     (verify_ice.py on each of the N largest "ice caves throughout" of results.py that has no
# check; a cavern the game agrees on, block for block, gets a line "SEED VOLUME biome" in checked.txt)
cd "$(dirname "$0")" || exit 1
python3 results.py --top 60 | awk -F'|' 'NF >= 5 && $2 ~ /[0-9],[0-9]/ && $5 ~ /^ *$/ {gsub(/[ ,]/, "", $2); gsub(/^ +| +$/, "", $3); gsub(/^ +| +$/, "", $4); print $3, $4, $2}' | head -n ${1:-8} | while read seed x y z volume; do
    out=$(python3 verify_ice.py $seed $x $y $z 2>&1 | tail -n 1)
    echo "$seed $volume: $out"
    game=$(echo "$out" | sed -n 's/.*ice_caves \([0-9,]*\) of \([0-9,]*\) blocks.*/\1 \2/p' | tr -d ',')
    [ "$game" = "$volume $volume" ] && echo "$seed $volume biome" >> checked.txt
done
