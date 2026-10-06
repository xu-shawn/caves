#!/bin/bash
# restarts only the g7 seed gate (the workers keep running) on the next block of seeds: GATE_THREADS, PRINT from the environment
cd /root/caves || exit 1
ps -eo comm,pcpu --sort=-pcpu | awk '{a[$1]+=$2} END {for (k in a) if (a[k] > 20) printf "%s %.0f  ", k, a[k]; print ""}'
pkill -x seedgate5; sleep 1
LAST=$(awk '{print $1}' runs/g7/gate.txt | sort -n | tail -1); FIRST=$(( (LAST / 100000000000 + 2) * 100000000000 )); PRINT=${PRINT:--0.4433}
echo "$FIRST $(date -u +%FT%TZ) seedgate5 on ${GATE_THREADS:-24} threads, print <= $PRINT (before: up to about $LAST)" >> runs/g7/gate_first.txt
echo "$(date -u +%FT%TZ) gate: ${GATE_THREADS:-24} threads, print <= $PRINT, from $FIRST; workers $(pgrep -x python3 | wc -l); from seed line $(wc -l < runs/g7/seeds.txt)" >> runs/g7/settings.txt
before=$(wc -l < runs/g7/gate.txt)
nohup ./seedgate5 $FIRST 9000000000000000000 ${GATE_THREADS:-24} $PRINT >> runs/g7/gate.txt 2>/dev/null &
sleep 90; echo "gate from $FIRST on ${GATE_THREADS:-24} threads: $(( $(wc -l < runs/g7/gate.txt) - before )) lines in 90 s; seeds done $(wc -l < runs/g7/seeds.txt)"
ps -eo comm,pcpu --sort=-pcpu | awk '{a[$1]+=$2} END {for (k in a) if (a[k] > 20) printf "%s %.0f  ", k, a[k]; print ""}'; date -u +%T
