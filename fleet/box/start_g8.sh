#!/bin/bash
# run g8: seedgate6 (the four cheese phases on the GPU, the rest as seedgate5) on the seeds after g7's, and the workers
# on its list. Settings from the environment (GATE_THREADS PRINT TERRAIN WORKERS MIN_CUT ...). ONLY_GATE=1: restart the gate alone.
cd /root/caves || exit 1
export PATH=/usr/local/cuda/bin:$PATH LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
[ -x seedgate6 ] || { echo "no seedgate6"; exit 1; }
mkdir -p runs/g8/claims
PRINT=${PRINT:--0.4433}; TERRAIN=${TERRAIN:-0.85}; GATE_THREADS=${GATE_THREADS:-12}
if [ -z "$ONLY_GATE" ]; then pkill -x python3; fi
pkill -x seedgate3; pkill -x seedgate4; pkill -x seedgate5; pkill -x seedgate6; sleep 1
if [ -z "$ONLY_GATE" ]; then pkill -x cavepairs3; pkill -x formulacave; pkill -x cavemeasure; sleep 1; command cp -f search3_new.py search3.py; chmod +x search3.py; fi
LAST=$(cat runs/g7/gate.txt runs/g8/gate.txt 2>/dev/null | awk '{print $1}' | sort -n | tail -1); NEXT=$(grep -o "next seed [0-9]*" runs/g8/gate.log 2>/dev/null | tail -1 | awk '{print $3}')
[ -n "$NEXT" ] && [ "$NEXT" -gt "$LAST" ] && LAST=$NEXT
FIRST=$(( (LAST / 100000000000 + 2) * 100000000000 ))
echo "$FIRST $(date -u +%FT%TZ) seedgate6 (GPU phase scan; terrain <= -$TERRAIN) on $GATE_THREADS threads, print <= $PRINT" >> runs/g8/gate_first.txt
nohup ./seedgate6 $FIRST 9000000000000000000 $GATE_THREADS $PRINT 0.55 0.03 0.595 0.06 $TERRAIN >> runs/g8/gate.txt 2>> runs/g8/gate.log &
if [ -z "$ONLY_GATE" ]; then
    awk '{print $1}' runs/g8/seeds.txt 2>/dev/null | sort > /tmp/done8.txt
    n=0; for c in $(ls -t runs/g8/claims | head -400); do grep -qx "$c" /tmp/done8.txt || { rm -f runs/g8/claims/$c; n=$((n + 1)); }; done
    export BEST_START=44324485 RATIO=${RATIO:-0} FLOOR=${FLOOR:-28e6} F_MID=${F_MID:-25e6} O_MID=${O_MID:-34e6} F_LOW=${F_LOW:-23e6} O_HIGH=${O_HIGH:-38e6} PER_SEED=40 SCANNER=cavepairs3 TOP=600 PLACES=${PLACES:-5000} MIN_SCORE=${MIN_SCORE:-2000} MIN_CUT=${MIN_CUT:-294} S2_MIN=${S2_MIN:-21e6} TERRAIN_DIV=1e12
    sleep 15
    for i in $(seq 1 ${WORKERS:-30}); do nohup ./search3.py g8 runs/g8/gate.txt ${TAKE:--0.4} 4 >> runs/g8/search$i.out 2>&1 & done
fi
echo "$(date -u +%FT%TZ) gate: seedgate6 from $FIRST, $GATE_THREADS threads, terrain <= -$TERRAIN, print <= $PRINT; workers: $(pgrep -x python3 | wc -l); exact rule as g7; MIN_CUT ${MIN_CUT:-294}; from seed line $(cat runs/g8/seeds.txt 2>/dev/null | wc -l)" >> runs/g8/settings.txt
sleep 75; tail -n 2 runs/g8/gate.log; echo "workers: $(pgrep -x python3 | wc -l); gate: $(pgrep -x seedgate6 | wc -l) from $FIRST; gate lines: $(wc -l < runs/g8/gate.txt); g8 seeds done: $(cat runs/g8/seeds.txt 2>/dev/null | wc -l)"
ps -eo comm,pcpu --sort=-pcpu | awk '{a[$1]+=$2} END {for (k in a) if (a[k] > 20) printf "%s %.0f  ", k, a[k]; print ""}'; nvidia-smi --query-gpu=utilization.gpu,temperature.gpu --format=csv,noheader; date -u +%T
