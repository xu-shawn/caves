#!/bin/bash
# The frontier caverns of caverns.csv against the real game code (Minecraft 26.3 server jar, large-biomes preset):
#   verified_game.txt   their volumes by java/CaveMeasure (measure_lb.sh), started at the /tp point
#   verified_biome.txt  "cells tx ty tz OWN GAMEDEEP DIFFER": the biome of every 4 x 4 x 4 cell that holds air of the cavern,
#                       asked of the game's biome source (java/BiomeCells, strict: deep dark only where it wins outright, which
#                       is cubiomes' rule for the ties) against cubiomes' answer that ddmeasure counts with
# frontier.py reads both. usage: verify.sh [THREADS=4]
cd "$(dirname "$0")"; S=-5296147297896900941; T=${1:-4}; mkdir -p export
python3 - <<'PY' >| verify_in.txt
import csv
for r in csv.DictReader(open('caverns.csv')):
    if '1' in (r['frontier_total'], r['frontier_free'], r['frontier_strict']): print('-5296147297896900941', r['tp_x'], r['tp_y'], r['tp_z'])
PY
done=$(cut -d' ' -f1-4 verified_game.in 2>/dev/null)
grep -vxF -f <(cat verified_game.in 2>/dev/null; echo "#") verify_in.txt >| verify_new.txt
if [ -s verify_new.txt ]; then
    ./measure_lb.sh $T < verify_new.txt >> verified_game.txt && cat verify_new.txt >> verified_game.in
    while read -r s x y z; do
        f=export/cells_${s}_${x}_${y}_${z}.bin
        [ -s "$f" ] || echo "$s $x $y $z" | DD_EXPORT_DIR=export ./ddmeasure 1 > /dev/null
        ./measure_lb.sh -c BiomeCells $s $f strict 2>/dev/null | sed 's/^.*STDOUT\]: //' | awk -v x=$x -v y=$y -v z=$z '/^own cells/ {own = $3; deep = $9} /^cells where the game and cubiomes differ/ {differ = $NF} END {print "cells", x, y, z, own, deep, differ}' >> verified_biome.txt
    done < verify_new.txt
fi
echo "verified: $(wc -l < verified_game.txt) volumes, $(wc -l < verified_biome.txt) biome checks"
