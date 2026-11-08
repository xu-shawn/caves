#!/bin/bash
# icecaves/square.sh: the ice caverns of one square of a world, start to end.
#   1. icesites     the places whose climate is inside the ice caves box (a lattice of 128 blocks, one a 512-block group)
#   2. iceformula   a quick score of the air within 192 blocks of each (lattice of 64, y 40 down); the best 15 in 100 again
#                   within 256 blocks on a lattice of 32
#   3. icemeasure   the exact cavern, with its blocks in ice caves, at the NI best by ice-caves cluster and the NP best
#                   by a cluster that is ice caves throughout
# usage: square.sh SEED X0 Z0 X1 Z1 WORK      env THREADS=12 NI=150 NP=250
# Appends the caverns to WORK/../results_SEED.txt and a line to WORK/../squares_SEED.log.
D="$(cd "$(dirname "$0")" && pwd)"; S=$1; W=$6; T=${THREADS:-12}; mkdir -p "$W"; t0=$(date +%s)
"$D/icesites" $S $2 $3 $4 $5 $T > "$W/sites.txt" 2> "$W/sites.log" || exit 1
"$D/iceformula" $T 192 64 40 < "$W/sites.txt" > "$W/quick.txt" || exit 1
n=$(wc -l < "$W/quick.txt")
awk '{s = $7 > $15 ? $7 : $15; print s, $1, $2, $3, $4}' "$W/quick.txt" | sort -k1,1nr | head -n $((n * 15 / 100 + 1)) | cut -d' ' -f2- > "$W/sites_full.txt"
"$D/iceformula" $T 256 32 < "$W/sites_full.txt" > "$W/full.txt" || exit 1
{ sort -k7,7nr "$W/full.txt" | head -n ${NI:-150} | awk '{print $1, $11, $12, $13, "| by ice", $7, "of", $9, "pure", $15}'
  sort -k15,15nr "$W/full.txt" | head -n ${NP:-250} | awk '{print $1, $17, $18, $19, "| by pure", $15, "ice", $7, "of", $9}'; } | awk '!seen[$2 " " $3 " " $4]++' > "$W/measure_in.txt"
"$D/icemeasure" $T < "$W/measure_in.txt" > "$W/measured.txt" || exit 1
cat "$W/measured.txt" >> "$W/../results_$S.txt"
echo "$S $2 $3 $4 $5: $n places, $(wc -l < "$W/sites_full.txt") scored in full, $(wc -l < "$W/measured.txt") measured, $(( $(date +%s) - t0 )) s; best fully ice $(awk '$4 > 0 && $4 == $(NF == 0 ? 1 : 0) {}; {for (i = 1; i <= NF; i++) if ($i == "ice" && $(i - 2) == "seconds") {if ($(i + 1) == $4 && $4 > b) b = $4; if ($(i + 1) > m) m = $(i + 1); break}} END {print b + 0 ", most ice " m + 0}' "$W/measured.txt")" >> "$W/../squares_$S.log"
rm -f "$W/sites.txt" "$W/quick.txt" "$W/sites_full.txt"
