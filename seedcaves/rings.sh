#!/bin/bash
# The ring scanner over the ladder of distances (seedcaves/ringpairs.cpp), one file of sites per ring: ring_<outer>.txt
cd "$(dirname "$0")"; S=${SEED:--5296147297896900941}; T=${THREADS:-6}; TOP=${TOP:-100}; P=${PREFIX:-ring}   # TOP: sites a ring by cluster, and as many by free; PREFIX: of the output files
prev=0
for out in 2000 4000 8000 16000 32000 64000 128000 256000 512000 1024000 2048000 4096000 8192000; do   # (a first run on a ladder of factor 4, 60 sites a ring: ladder4/)
    pairs=800000; places=${PLACES:-4000}; [ $out -ge 512000 ] && { pairs=2000000; places=$((2 * ${PLACES:-4000})); }
    DIRECT=1 RING_IN=$prev RING_OUT=$out SURFACE_MIN=0 MIN_POINTS=8 ./ringpairs $S $pairs $TOP $T $places 0 >| ${P}_$out.txt 2>| ${P}_$out.log
    echo "ring $prev..$out: $(wc -l < ${P}_$out.txt) sites; $(tail -n 1 ${P}_$out.log | grep -o 'clusters at.*')"
    prev=$out
done
# the rest of the world: the pairs picked over the whole world as in the search, their places beyond the last ring
RING_IN=$prev MIN_POINTS=8 ./ringpairs $S ${OUTER_PAIRS:-600000} $((TOP * 3 / 2)) $T ${OUTER_PLACES:-15000} 0 >| ${P}_30000000.txt 2>| ${P}_30000000.log
echo "ring $prev..border: $(wc -l < ${P}_30000000.txt) sites; $(tail -n 1 ${P}_30000000.log | grep -o 'clusters at.*')"
date -u +%T
