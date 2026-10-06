#!/bin/bash
# The cubiomes objects the programs link against (vendor/cub_*.o). Run once: vendor/build.sh
cd "$(dirname "$0")" || exit 1
for f in noise biomes layers biomenoise; do gcc -O3 -march=native -fwrapv -c -o cub_$f.o cubiomes/$f.c || exit 1; done
echo "built: $(ls cub_*.o | tr '\n' ' ')"
