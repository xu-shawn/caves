#!/bin/bash
# run g7: seedgate5 (score = wide gate + 0.75 x 512-block gate + terrain / 100, faster fields) on the seeds after g6's,
# and the workers on its list. Settings from the environment (GATE_THREADS PRINT WORKERS MIN_CUT ...).
cd /root/caves || exit 1
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate5.cpp cub_noise.o -lpthread -o seedgate5_new 2>&1 | grep -i " error" -A3
[ -x seedgate5_new ] || { echo "build failed"; exit 1; }
python3 -c "import ast; ast.parse(open('search3_new.py').read())" || { echo "search3_new.py does not parse"; exit 1; }
./seedgate5_new 7000000000000000 7000000100000000 8 9 0.55 0.03 0.595 0.06 0.55 100 0.75 9 | sort | head -3
pkill -x python3; pkill -x seedgate3; pkill -x seedgate4; pkill -x seedgate5; sleep 1; pkill -x cavepairs3; pkill -x formulacave; pkill -x cavemeasure; sleep 1
command mv -f seedgate5_new seedgate5; command cp -f search3_new.py search3.py; chmod +x search3.py
mkdir -p runs/g7/claims
PRINT=${PRINT:--0.4433}
LAST=$(cat runs/g6/gate.txt runs/g7/gate.txt 2>/dev/null | awk '{print $1}' | sort -n | tail -1); FIRST=$(( (LAST / 100000000000 + 2) * 100000000000 ))
awk '{print $1}' runs/g7/seeds.txt 2>/dev/null | sort > /tmp/done7.txt
n=0; for c in $(ls -t runs/g7/claims | head -400); do grep -qx "$c" /tmp/done7.txt || { rm -f runs/g7/claims/$c; n=$((n + 1)); }; done
echo "$FIRST $(date -u +%FT%TZ) seedgate5 (wide + 0.75 x gate512 + terrain/100) on ${GATE_THREADS:-24} threads, print <= $PRINT" >> runs/g7/gate_first.txt
nohup ./seedgate5 $FIRST 9000000000000000000 ${GATE_THREADS:-24} $PRINT >> runs/g7/gate.txt 2>/dev/null &
export BEST_START=44324485 RATIO=${RATIO:-0} FLOOR=${FLOOR:-28e6} F_MID=${F_MID:-25e6} O_MID=${O_MID:-34e6} F_LOW=${F_LOW:-23e6} O_HIGH=${O_HIGH:-38e6} PER_SEED=40 SCANNER=cavepairs3 TOP=600 PLACES=${PLACES:-5000} MIN_SCORE=${MIN_SCORE:-2000} MIN_CUT=${MIN_CUT:-294} S2_MIN=${S2_MIN:-21e6} TERRAIN_DIV=1e12
echo "$(date -u +%FT%TZ) gate: seedgate5 from $FIRST, ${GATE_THREADS:-24} threads, print <= $PRINT; workers: ${WORKERS:-22}; exact for F >= max($FLOOR, $RATIO x best) or (F >= $F_MID and O >= $O_MID) or (F >= $F_LOW and O >= $O_HIGH); MIN_CUT $MIN_CUT, PLACES $PLACES, MIN_SCORE $MIN_SCORE, from seed line $(cat runs/g7/seeds.txt 2>/dev/null | wc -l)" >> runs/g7/settings.txt
sleep 15
for i in $(seq 1 ${WORKERS:-22}); do nohup ./search3.py g7 runs/g7/gate.txt ${TAKE:--0.4} 4 >> runs/g7/search$i.out 2>&1 & done
sleep 45; echo "workers: $(pgrep -x python3 | wc -l); gate: $(pgrep -x seedgate5 | wc -l) from $FIRST; gate lines: $(wc -l < runs/g7/gate.txt) in 60 s; $n seeds back on the list; g7 seeds done: $(cat runs/g7/seeds.txt 2>/dev/null | wc -l)"; date -u +%T
