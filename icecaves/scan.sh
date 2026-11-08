#!/bin/bash
# icecaves/scan.sh: square.sh over the squares of a world (4,000,000 blocks a side), nearest the origin first.
# usage: scan.sh SEED [FIRST=1] [LAST=224]      (square 0 is the one around the origin; work/squares_SEED.log has the progress;
#        touch work/STOP to end after the square in hand)
D="$(cd "$(dirname "$0")" && pwd)"; S=$1
python3 - "$S" "${2:-1}" "${3:-224}" <<'PY' | while read i j x0 z0 x1 z1; do [ -e "$D/work/STOP" ] && break; "$D/square.sh" $S $x0 $z0 $x1 $z1 "$D/work/${S}_${i}_${j}" || echo "$S $i $j failed" >> "$D/work/squares_$S.log"; done
import sys
tiles = sorted(((max(abs(i), abs(j)), abs(i) + abs(j), i, j) for i in range(-7, 8) for j in range(-7, 8)))
for n, (_, _, i, j) in enumerate(tiles):
    if int(sys.argv[2]) <= n <= int(sys.argv[3]): print(i, j, max(i * 4000000 - 2000000, -29999872), max(j * 4000000 - 2000000, -29999872), min(i * 4000000 + 2000000, 29999872), min(j * 4000000 + 2000000, 29999872))
PY
