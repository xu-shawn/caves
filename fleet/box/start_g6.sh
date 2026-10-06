#!/bin/bash
# run g6: the wide seed gate (seedgate4: 1280-block windows) in place of seedgate3, on a new seed range, and the
# workers on its list. Settings from the environment (GATE_THREADS PRINT FIRST WORKERS MIN_CUT ...).
cd /root/caves || exit 1
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate4.cpp cub_noise.o -lpthread -o seedgate4_new 2>&1 | grep -i " error" -A3
[ -x seedgate4_new ] || { echo "build failed"; exit 1; }
python3 -c "import ast; ast.parse(open('search3_new.py').read())" || { echo "search3_new.py does not parse"; exit 1; }
./seedgate4_new 7000000000000000 7000000100000000 8 9 | sort | head -3
pkill -x python3; pkill -x seedgate3; pkill -x seedgate4; sleep 1; pkill -x cavepairs3; pkill -x formulacave; pkill -x cavemeasure; sleep 1
command mv -f seedgate4_new seedgate4; command cp -f search3_new.py search3.py; chmod +x search3.py
mkdir -p runs/g6/claims
FIRST=${FIRST:-6000000000000}; PRINT=${PRINT:--0.2045}
if [ -s runs/g6/gate.txt ]; then LAST=$(awk '{print $1}' runs/g6/gate.txt | sort -n | tail -1); FIRST=$(( (LAST / 100000000000 + 2) * 100000000000 )); fi
awk '{print $1}' runs/g6/seeds.txt 2>/dev/null | sort > /tmp/done6.txt
n=0; for c in $(ls -t runs/g6/claims | head -400); do grep -qx "$c" /tmp/done6.txt || { rm -f runs/g6/claims/$c; n=$((n + 1)); }; done
echo "$FIRST $(date -u +%FT%TZ) seedgate4 (wide gate, 1280-block windows, pair 1,000) on ${GATE_THREADS:-24} threads, print <= $PRINT" >> runs/g6/gate_first.txt
nohup ./seedgate4 $FIRST 9000000000000000000 ${GATE_THREADS:-24} $PRINT >> runs/g6/gate.txt 2>/dev/null &
export BEST_START=44324485 RATIO=${RATIO:-0} FLOOR=${FLOOR:-28e6} F_MID=${F_MID:-25e6} O_MID=${O_MID:-34e6} F_LOW=${F_LOW:-23e6} O_HIGH=${O_HIGH:-38e6} PER_SEED=40 SCANNER=cavepairs3 TOP=600 PLACES=${PLACES:-5000} MIN_SCORE=${MIN_SCORE:-2000} MIN_CUT=${MIN_CUT:-294} S2_MIN=${S2_MIN:-21e6} TERRAIN_DIV=250
echo "$(date -u +%FT%TZ) gate: seedgate4 from $FIRST, ${GATE_THREADS:-24} threads, print <= $PRINT; workers: ${WORKERS:-22}, take <= ${TAKE:--0.2}; exact for F >= max($FLOOR, $RATIO x best) or (F >= $F_MID and O >= $O_MID) or (F >= $F_LOW and O >= $O_HIGH); MIN_CUT $MIN_CUT, PLACES $PLACES, MIN_SCORE $MIN_SCORE, from seed line $(cat runs/g6/seeds.txt 2>/dev/null | wc -l)" >> runs/g6/settings.txt
sleep 20
for i in $(seq 1 ${WORKERS:-22}); do nohup ./search3.py g6 runs/g6/gate.txt ${TAKE:--0.2} 4 >> runs/g6/search$i.out 2>&1 & done
sleep 40; echo "workers: $(pgrep -x python3 | wc -l); gate: $(pgrep -x seedgate4 | wc -l) from $FIRST; gate lines: $(wc -l < runs/g6/gate.txt); $n seeds back on the list; g6 seeds done: $(cat runs/g6/seeds.txt 2>/dev/null | wc -l)"; date -u +%T
