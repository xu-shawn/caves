#!/bin/bash
# run g12: seedgate10 (the cheap looks on CPU threads: cpuprelook.hpp; the GPU scans and gives the few seeds past them the first look; the CPU gate adds
# the percolation term) on the seeds after g11's, and the workers on its list. Settings from the
# Since 11:13 UTC the terrain score counts as terrain / 30 in the gate's score (was / 100: a fit on 93,000 scanned seeds gives / 25 for caverns of 38M+), with
# the cuts moved by the mean shift (-0.013).
# environment (GATE_THREADS PRINT PRE PERC TERRAIN C8 W8 C7 W7 TABLE_BATCH WORKERS PAIRS MIN_CUT MIN_SCORE FO_MIN O_WEIGHT ...).
# ONLY_GATE=1: restart the gate alone. ONLY_WORKERS=1: restart the workers alone.
cd /root/caves || exit 1
export PATH=/usr/local/cuda/bin:$PATH LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
[ -x seedgate10 ] || { echo "no seedgate10"; exit 1; }
mkdir -p runs/g12/claims
PRINT=${PRINT:--0.585}; TERRAIN=${TERRAIN:-0.45}; GATE_THREADS=${GATE_THREADS:-27}; export PRE=${PRE:--0.466} PERC=${PERC:-0.0912} TABLE_BATCH=${TABLE_BATCH:-1024} CUTY=${CUTY:--0.1748} CUTM=${CUTM:--0.202} CUT2=${CUT2:--0.2076} SKIP=${SKIP:--0.300}
if [ -z "$ONLY_GATE" ]; then pkill -x python3; fi
if [ -z "$ONLY_WORKERS" ]; then pkill -x seedgate5; pkill -x seedgate6; pkill -x seedgate7; pkill -x seedgate8; pkill -x seedgate9; pkill -x seedgate10; fi
sleep 1
if [ -z "$ONLY_GATE" ]; then pkill -x cavepairs3; pkill -x formulacave; pkill -x cavemeasure; sleep 1; command cp -f search3_new.py search3.py; chmod +x search3.py; fi
if [ -z "$ONLY_WORKERS" ]; then
    LAST=$(cat runs/g10/gate.log runs/g11/gate.log runs/g12/gate.log 2>/dev/null | grep -o "next seed [0-9]*" | awk '{print $3}' | sort -n | tail -1)
    FIRST=$(( (LAST / 100000000000 + 2) * 100000000000 ))
    echo "$FIRST $(date -u +%FT%TZ) seedgate10 on $GATE_THREADS threads: terrain <= -$TERRAIN, phases ${C8:-0.542} +- ${W8:-0.045} and ${C7:-0.584} +- ${W7:-0.10}, CPU looks <= $CUTY, $CUTM and $CUT2, terrain / ${TDIV:-30} in the score, first look <= $PRE, print score - $PERC x volume <= $PRINT" >> runs/g12/gate_first.txt
    nohup ./seedgate10 $FIRST 9000000000000000000 $GATE_THREADS $PRINT ${C8:-0.542} ${W8:-0.045} ${C7:-0.584} ${W7:-0.10} $TERRAIN ${TDIV:-30} >> runs/g12/gate.txt 2>> runs/g12/gate.log &
    echo "$(date -u +%FT%TZ) gate: seedgate10 from $FIRST, $GATE_THREADS threads, terrain <= -$TERRAIN, CPU looks <= $CUTY, $CUTM and $CUT2, terrain / ${TDIV:-30} in the score, first look <= $PRE, print <= $PRINT (weight $PERC); from seed line $(cat runs/g12/seeds.txt 2>/dev/null | wc -l)" >> runs/g12/settings.txt
fi
if [ -z "$ONLY_GATE" ]; then
    awk '{print $1}' runs/g12/seeds.txt 2>/dev/null | sort > /tmp/done10.txt
    n=0; for c in $(ls -t runs/g12/claims | head -400); do grep -qx "$c" /tmp/done10.txt || { rm -f runs/g12/claims/$c; n=$((n + 1)); }; done
    export BEST_START=50412026 RATIO=0 FLOOR=${FLOOR:-28e6} F_MID=${F_MID:-25e6} O_MID=${O_MID:-34e6} F_LOW=${F_LOW:-23e6} O_HIGH=${O_HIGH:-38e6} FO_MIN=${FO_MIN:-107e6} O_WEIGHT=${O_WEIGHT:-2} PER_SEED=40 SCANNER=cavepairs3 PAIRS=${PAIRS:-200000} TOP=600 PLACES=${PLACES:-1250} MIN_SCORE=${MIN_SCORE:-2200} MIN_CUT=${MIN_CUT:-292} S2_MIN=${S2_MIN:-21e6} CUT_FREE=${CUT_FREE:--0.613} TERRAIN_DIV=1e12
    [ -z "$ONLY_WORKERS" ] && sleep 15
    for i in $(seq 1 ${WORKERS:-35}); do nohup ./search3.py g12 runs/g12/gate.txt ${TAKE:--0.5} ${WTHREADS:-1} >> runs/g12/search$i.out 2>&1 & done
    echo "$(date -u +%FT%TZ) workers: ${WORKERS:-35}; exact for F + $O_WEIGHT O >= $FO_MIN and (F >= $FLOOR or (F >= $F_MID and O >= $O_MID) or (F >= $F_LOW and O >= $O_HIGH)); PAIRS $PAIRS, MIN_CUT $MIN_CUT (none for gate values <= $CUT_FREE), PLACES $PLACES, MIN_SCORE $MIN_SCORE; $n seeds back on the list; from seed line $(cat runs/g12/seeds.txt 2>/dev/null | wc -l)" >> runs/g12/settings.txt
fi
sleep 75; tail -n 2 runs/g12/gate.log | cut -c1-420; echo "workers: $(pgrep -x python3 | wc -l); gate: $(pgrep -x seedgate10 | wc -l); gate lines: $(wc -l < runs/g12/gate.txt); g12 seeds done: $(cat runs/g12/seeds.txt 2>/dev/null | wc -l)"
nvidia-smi --query-gpu=utilization.gpu,memory.used,temperature.gpu --format=csv,noheader; date -u +%T
