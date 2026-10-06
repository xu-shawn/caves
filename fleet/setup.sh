#!/bin/bash
# Uploads the cave search (all native: the pair scanner, the formula cavern predictor, the native game terrain generator
# and measuring tool, the driver, cubiomes for the Perlin tables and the surface estimate) to a rented box and builds it
# in /root/caves.     usage: setup.sh NAME
export FLEET_PREFIX=cv- VAST_SSH_KEY=${VAST_SSH_KEY:-$HOME/.ssh/vastai}
L="$(cd "$(dirname "$0")/../.." && pwd)"; B="$L/desert-search/deploy/box.sh"; N=$1
tmp=$(mktemp --suffix=.tgz)
tar czf "$tmp" -C "$L/caves" cavepairs.cpp formulacave.cpp cavenoise.hpp name_hashes.h noise_params.h search2.py native/density_gen.hpp native/gamegen_types.hpp native/gamegen.hpp native/cavemeasure.cpp \
    -C "$L/caves/vendor" cubiomes || exit 1
ok=0
for try in 1 2 3 4 5 6; do
    timeout 60 "$B" "$N" 'mkdir -p /root/caves' 2>/dev/null && timeout 180 "$B" "$N" put "$tmp" /root/caves.tgz 2>/dev/null && { ok=1; break; }
    sleep 15
done
rm -f "$tmp"; [ $ok = 1 ] || { echo "$N: upload failed"; exit 1; }
timeout 900 "$B" "$N" 'cd /root/caves && tar xzf ../caves.tgz && (command -v g++ >/dev/null || (apt-get update -qq && apt-get install -y -qq g++ gcc python3 >/dev/null 2>&1)); for f in noise biomes layers biomenoise; do gcc -O3 -march=native -fwrapv -c -o cub_$f.o cubiomes/$f.c; done; g++ -O3 -march=native -std=c++17 -I cubiomes cavepairs.cpp cub_*.o -lpthread -o cavepairs 2>&1 | grep -i " error"; g++ -O3 -march=native -std=c++17 -I cubiomes formulacave.cpp cub_*.o -lpthread -o formulacave 2>&1 | grep -i " error"; (cd native && g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../cubiomes cavemeasure.cpp ../cub_noise.o -lpthread -o cavemeasure 2>&1 | grep -i " error"); ls cavepairs formulacave native/cavemeasure > /dev/null && echo "'"$N"': built, $(nproc) threads, $(free -g | awk "NR==2 {print \$2}") GB"'
