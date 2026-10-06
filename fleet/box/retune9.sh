#!/bin/bash
# restarts the g9 workers alone (the gate keeps running) with search3_new.py; settings from the environment
# (WORKERS FO_MIN FLOOR F_MID O_MID F_LOW O_HIGH MIN_CUT PLACES MIN_SCORE), seeds in the works go back on the list
cd /root/caves || exit 1
python3 -c "import ast; ast.parse(open('search3_new.py').read())" || { echo "search3_new.py does not parse"; exit 1; }
pkill -x python3; sleep 1; pkill -x cavepairs3; pkill -x formulacave; pkill -x cavemeasure; sleep 1
command cp -f search3_new.py search3.py; chmod +x search3.py
awk '{print $1}' runs/g9/seeds.txt | sort > /tmp/done9.txt
n=0; for c in $(ls -t runs/g9/claims | head -400); do grep -qx "$c" /tmp/done9.txt || { rm -f runs/g9/claims/$c; n=$((n + 1)); }; done
export BEST_START=44324485 RATIO=0 FLOOR=${FLOOR:-28e6} F_MID=${F_MID:-25e6} O_MID=${O_MID:-34e6} F_LOW=${F_LOW:-23e6} O_HIGH=${O_HIGH:-38e6} FO_MIN=${FO_MIN:-63e6} O_WEIGHT=${O_WEIGHT:-1} PER_SEED=40 SCANNER=cavepairs3 PAIRS=${PAIRS:-600000} TOP=600 PLACES=${PLACES:-5000} MIN_SCORE=${MIN_SCORE:-2000} MIN_CUT=${MIN_CUT:-294} S2_MIN=${S2_MIN:-21e6} TERRAIN_DIV=1e12
echo "$(date -u +%FT%TZ) workers: ${WORKERS:-36}; exact for F + $O_WEIGHT O >= $FO_MIN and (F >= $FLOOR or (F >= $F_MID and O >= $O_MID) or (F >= $F_LOW and O >= $O_HIGH)); PAIRS $PAIRS, MIN_CUT $MIN_CUT, PLACES $PLACES, MIN_SCORE $MIN_SCORE; from seed line $(wc -l < runs/g9/seeds.txt)" >> runs/g9/settings.txt
for i in $(seq 1 ${WORKERS:-36}); do nohup ./search3.py g9 runs/g9/gate.txt ${TAKE:--0.4} 4 >> runs/g9/search$i.out 2>&1 & done
sleep 3; echo "workers: $(pgrep -x python3 | wc -l); gate: $(pgrep -x seedgate7 | wc -l); $n seeds back on the list; g9 seeds done: $(wc -l < runs/g9/seeds.txt); gate lines $(wc -l < runs/g9/gate.txt)"; date -u +%T
