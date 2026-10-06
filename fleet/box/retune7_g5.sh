#!/bin/bash
# restarts the g5 workers with search3_new.py (no rebuild); settings from the environment
# (MIN_CUT PLACES MIN_SCORE RATIO FLOOR F_MID O_MID F_LOW O_HIGH WORKERS S2_MIN), seeds in the works go back on the list
cd /root/caves || exit 1
python3 -c "import ast; ast.parse(open('search3_new.py').read())" || { echo "search3_new.py does not parse"; exit 1; }
pkill -x python3; sleep 1; pkill -x cavepairs3; pkill -x formulacave; pkill -x cavemeasure; sleep 1
command cp -f search3_new.py search3.py; chmod +x search3.py
awk '{print $1}' runs/g5/seeds.txt | sort > /tmp/done5.txt
n=0; for c in $(ls -t runs/g5/claims | head -400); do grep -qx "$c" /tmp/done5.txt || { rm -f runs/g5/claims/$c; n=$((n + 1)); }; done
export BEST_START=44324485 RATIO=${RATIO:-0} FLOOR=${FLOOR:-28e6} F_MID=${F_MID:-25e6} O_MID=${O_MID:-34e6} F_LOW=${F_LOW:-23e6} O_HIGH=${O_HIGH:-38e6} PER_SEED=40 SCANNER=cavepairs3 TOP=600 PLACES=${PLACES:-5000} MIN_SCORE=${MIN_SCORE:-2000} MIN_CUT=${MIN_CUT:-294} S2_MIN=${S2_MIN:-21e6}
echo "$(date -u +%FT%TZ) workers: exact for F >= max($FLOOR, $RATIO x best) or (F >= $F_MID and O >= $O_MID) or (F >= $F_LOW and O >= $O_HIGH); MIN_CUT $MIN_CUT, PLACES $PLACES, MIN_SCORE $MIN_SCORE, ${WORKERS:-22} workers, from seed line $(wc -l < runs/g5/seeds.txt)" >> runs/g5/settings.txt
for i in $(seq 1 ${WORKERS:-22}); do nohup ./search3.py g5 runs/g5/gate.txt -0.314 4 >> runs/g5/search$i.out 2>&1 & done
sleep 3; echo "workers: $(pgrep -x python3 | wc -l); gate: $(pgrep -x seedgate3 | wc -l); $n seeds back on the list; g5 seeds done: $(wc -l < runs/g5/seeds.txt)"; date -u +%T
