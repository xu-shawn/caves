#!/bin/bash
# icecaves/fleet/setup.sh: uploads the ice caves search as it stands and builds it in /root/ice/icecaves on a rented box
# with a CUDA card (the repository's layout is kept: the cave search's files it needs in /root/ice, the ice caves search in
# /root/ice/icecaves, so every build line is the one in the sources). Builds: cubiomes objects, icebiome.o, icepairs (the scanner), iceformula, icemeasure,
# icephase (the CPU gate, for checks) and icegate (the gate on the card).
# usage: fleet/setup.sh NAME          (NAME as registered by desert-search/deploy/rent.sh with FLEET_PREFIX=ice-)
export FLEET_PREFIX=ice- VAST_SSH_KEY=${VAST_SSH_KEY:-$HOME/.ssh/vastai}
L="$(cd "$(dirname "$0")/../.." && pwd)"; B="$L/../desert-search/deploy/box.sh"; N=$1     # L: the repository
tmp=$(mktemp --suffix=.tgz)
tar czf "$tmp" -C "$L" icecaves/icepairs.cpp icecaves/iceformula.cpp icecaves/icemeasure.cpp icecaves/icebiome.cpp icecaves/icebiome.hpp icecaves/icephase.cpp icecaves/icegate.cu icecaves/fastnoise.hpp \
    icecaves/seed.sh icecaves/gaterun.sh icecaves/index_coarse.txt icecaves/spec_p1.txt icecaves/fleet/start.sh icecaves/fleet/rate.sh \
    name_hashes.h noise_params.h seedcaves/cavenoise.hpp seedcaves/name_hashes.h seedcaves/noise_params.h \
    native/gamegen.hpp native/density_gen.hpp native/gamegen_types.hpp vendor/cubiomes vendor/build.sh || exit 1
ok=0
for try in 1 2 3 4 5 6; do
    timeout 60 "$B" "$N" 'mkdir -p /root/ice' 2>/dev/null && timeout 300 "$B" "$N" put "$tmp" /root/ice.tgz 2>/dev/null && { ok=1; break; }
    sleep 15
done
rm -f "$tmp"; [ $ok = 1 ] || { echo "$N: upload failed"; exit 1; }
timeout 1800 "$B" "$N" 'cd /root/ice && tar xzf ../ice.tgz && export PATH=/usr/local/cuda/bin:$PATH LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
(command -v g++ >/dev/null && command -v python3 >/dev/null && command -v bc >/dev/null) || (apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq g++ gcc python3 bc >/dev/null 2>&1)
fail() { echo "$1 FAILED"; }
bash vendor/build.sh || fail cubiomes
cd icecaves; F="-O3 -march=native -std=c++17 -ffp-contract=off"
g++ $F -I ../vendor/cubiomes -c icebiome.cpp -o icebiome.o || fail icebiome
g++ $F -I ../seedcaves -I ../vendor/cubiomes icepairs.cpp icebiome.o ../vendor/cub_*.o -lpthread -o icepairs || fail icepairs &
g++ $F -I ../native -I ../vendor/cubiomes iceformula.cpp icebiome.o ../vendor/cub_*.o -lpthread -o iceformula || fail iceformula &
g++ $F -I ../native -I ../vendor/cubiomes icemeasure.cpp icebiome.o ../vendor/cub_*.o -lpthread -o icemeasure || fail icemeasure &
g++ $F -I .. -I ../vendor/cubiomes icephase.cpp ../vendor/cub_*.o -lpthread -o icephase || fail icephase &
nvcc -O3 -I .. -I ../vendor/cubiomes icegate.cu -o icegate || fail icegate &
wait; chmod +x seed.sh gaterun.sh fleet/*.sh
ls -la icepairs iceformula icemeasure icephase icegate | awk "{print \$5, \$9}"; nproc; free -g | sed -n 2p; nvidia-smi -L'
