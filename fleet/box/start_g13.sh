#!/bin/bash
# run g13: the pipeline as run g12 ended (seedgate10: the cheap looks on CPU threads; the GPU scans and gives the few seeds past them the first look; the
# CPU gate adds the percolation term; the terrain score counts as terrain / 30), on a new box, from seed 2.0e14 up, and the workers on its list.
# Settings from the environment (GATE_THREADS PRINT PRE PERC TERRAIN TDIV C8 W8 C7 W7 CUTY CUTM CUT2 TABLE_BATCH WORKERS WTHREADS PAIRS PLACES
# MIN_CUT MIN_SCORE FO_MIN O_WEIGHT CUT_FREE GATE=seedgate10 ...). ONLY_GATE=1: restart the gate alone. ONLY_WORKERS=1: restart the workers alone.
# Keep gate threads + workers within the box's CPU quota: a container over its quota is frozen part of every period, the GPU thread too.
cd /root/caves || exit 1
export PATH=/usr/local/cuda/bin:$PATH LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
GATE=${GATE:-seedgate10}; [ -x $GATE ] || { echo "no $GATE"; exit 1; }
mkdir -p runs/g13/claims
PRINT=${PRINT:--0.600}; TERRAIN=${TERRAIN:-0.45}; GATE_THREADS=${GATE_THREADS:-27}; export PRE=${PRE:--0.466} PERC=${PERC:-0.0912} TABLE_BATCH=${TABLE_BATCH:-1024} CUTY=${CUTY:--0.1748} CUTM=${CUTM:--0.202} CUT2=${CUT2:--0.2076} SKIP=${SKIP:--0.300}
if [ -z "$ONLY_GATE" ]; then pkill -x python3; fi
if [ -z "$ONLY_WORKERS" ]; then pkill -x seedgate10; pkill -x seedgate11; pkill -x seedgate12; pkill -x seedgate13; fi
sleep 1
if [ -z "$ONLY_GATE" ]; then pkill -x cavepairs3; pkill -x cavepairs4; pkill -x cavepairs5; pkill -x formulacave; pkill -x cavemeasure; sleep 1; command cp -f search3_new.py search3.py; chmod +x search3.py; fi
if [ -z "$ONLY_WORKERS" ]; then
    LAST=$(cat runs/g13/gate.log 2>/dev/null | grep -o "next seed [0-9]*" | awk '{print $3}' | sort -n | tail -1)
    FIRST=$(( (${LAST:-199800000000000} / 100000000000 + 2) * 100000000000 ))
    echo "$FIRST $(date -u +%FT%TZ) $GATE on $GATE_THREADS threads: terrain <= -$TERRAIN, phases ${C8:-0.542} +- ${W8:-0.045} and ${C7:-0.584} +- ${W7:-0.10}, CPU looks <= $CUTY, $CUTM and $CUT2, terrain / ${TDIV:-30} in the score, first look <= $PRE, print score - $PERC x volume <= $PRINT" >> runs/g13/gate_first.txt
    nohup ./$GATE $FIRST 9000000000000000000 $GATE_THREADS $PRINT ${C8:-0.542} ${W8:-0.045} ${C7:-0.584} ${W7:-0.10} $TERRAIN ${TDIV:-30} >> runs/g13/gate.txt 2>> runs/g13/gate.log &
    echo "$(date -u +%FT%TZ) gate: $GATE from $FIRST, $GATE_THREADS threads, terrain <= -$TERRAIN, CPU looks <= $CUTY, $CUTM and $CUT2, terrain / ${TDIV:-30} in the score, first look <= $PRE, print <= $PRINT (weight $PERC); from seed line $(cat runs/g13/seeds.txt 2>/dev/null | wc -l)" >> runs/g13/settings.txt
fi
if [ -z "$ONLY_GATE" ]; then
    pgrep -x gpusrv > /dev/null || { [ -x gpusrv ] && (nohup ./gpusrv >> gpusrv.log 2>&1 &) && sleep 2; }   # the GPU's services for the workers: the scanner's terrain step (cavepairs4 with GPU_TERRAIN=1) and the exact measurer's corner densities (GPU_DENSITY=1)
    awk '{print $1}' runs/g13/seeds.txt 2>/dev/null | sort > /tmp/done10.txt
    n=0; for c in $(ls -t runs/g13/claims | head -400); do grep -qx "$c" /tmp/done10.txt || { rm -f runs/g13/claims/$c; n=$((n + 1)); }; done
    export BEST_START=50992456 RATIO=0 FLOOR=${FLOOR:-28e6} F_MID=${F_MID:-25e6} O_MID=${O_MID:-34e6} F_LOW=${F_LOW:-23e6} O_HIGH=${O_HIGH:-38e6} FO_MIN=${FO_MIN:-105e6} O_WEIGHT=${O_WEIGHT:-2} PER_SEED=40 SCANNER=${SCANNER:-cavepairs5} GPU_TERRAIN=${GPU_TERRAIN-1} GPU_TABLES=${GPU_TABLES-1} GPU_DENSITY=${GPU_DENSITY-1} GPU_FORMULA=${GPU_FORMULA-1} PAIRS=${PAIRS:-200000} TOP=600 PLACES=${PLACES:-1250} MIN_SCORE=${MIN_SCORE:-2200} MIN_CUT=${MIN_CUT:-292} S2_MIN=${S2_MIN:-21e6} CUT_FREE=${CUT_FREE:--0.613} TERRAIN_DIV=1e12
    [ -z "$ONLY_WORKERS" ] && sleep 15
    # the first GPU_MEASURERS workers take the exact measurer's densities from the GPU, the others make them on their own CPU: the card serves a
    # limited number, and a worker waiting for it leaves its CPU thread idle
    for i in $(seq 1 ${WORKERS:-35}); do if [ $i -le ${GPU_MEASURERS:-999} ]; then nohup ./search3.py g13 runs/g13/gate.txt ${TAKE:--0.5} ${WTHREADS:-1} >> runs/g13/search$i.out 2>&1 & else env -u GPU_DENSITY nohup ./search3.py g13 runs/g13/gate.txt ${TAKE:--0.5} ${WTHREADS:-1} >> runs/g13/search$i.out 2>&1 & fi; done
    echo "$(date -u +%FT%TZ) workers: ${WORKERS:-35}; exact for F + $O_WEIGHT O >= $FO_MIN and (F >= $FLOOR or (F >= $F_MID and O >= $O_MID) or (F >= $F_LOW and O >= $O_HIGH)); scanner $SCANNER (GPU terrain: ${GPU_TERRAIN:-off}; GPU tables and formula step: ${GPU_TABLES:-off}; exact measure with GPU densities: ${GPU_DENSITY:-off} for ${GPU_MEASURERS:-all} workers; formula stage on the GPU: ${GPU_FORMULA:-off}), PAIRS $PAIRS, MIN_CUT $MIN_CUT (none for gate values <= $CUT_FREE), PLACES $PLACES, MIN_SCORE $MIN_SCORE; $n seeds back on the list; from seed line $(cat runs/g13/seeds.txt 2>/dev/null | wc -l)" >> runs/g13/settings.txt
fi
sleep 75; tail -n 2 runs/g13/gate.log | cut -c1-420; echo "workers: $(pgrep -x python3 | wc -l); gate: $(pgrep -x $GATE | wc -l); gate lines: $(wc -l < runs/g13/gate.txt); g13 seeds done: $(cat runs/g13/seeds.txt 2>/dev/null | wc -l)"
nvidia-smi --query-gpu=utilization.gpu,memory.used,temperature.gpu --format=csv,noheader; date -u +%T
