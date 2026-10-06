#!/bin/bash
# Uploads the cave search as it stands (GPU phase scan and first look, the gate with its CPU looks, the pair scanner, the
# formula cavern predictor, the native game terrain generator and measuring tool, the driver, cubiomes for the Perlin
# tables and the surface estimate) to a rented box with CUDA and builds it in /root/caves.     usage: setup3.sh NAME
export FLEET_PREFIX=cv- VAST_SSH_KEY=${VAST_SSH_KEY:-$HOME/.ssh/vastai}
L="$(cd "$(dirname "$0")/../.." && pwd)"; B="$L/desert-search/deploy/box.sh"; N=$1
tmp=$(mktemp --suffix=.tgz)
tar czf "$tmp" -C "$L/caves" cavepairs3.cpp formulacave.cpp seedgate10.cpp seedgate11.cpp cavenoise.hpp cpuprelook.hpp tableseed.hpp analyticgate.hpp percgate.hpp name_hashes.h noise_params.h \
    gpuscan.cu gpuscan.h gputables.cu gpulook.cu search3.py stats/scanbench.cpp stats/lookcheck.cpp stats/gpucheck.cpp native/density_gen.hpp native/gamegen_types.hpp native/gamegen.hpp native/cavemeasure.cpp \
    -C "$L/caves/fleet/box" start_g13.sh -C "$L/caves/vendor" cubiomes || exit 1
ok=0
for try in 1 2 3 4 5 6; do
    timeout 60 "$B" "$N" 'mkdir -p /root/caves' 2>/dev/null && timeout 180 "$B" "$N" put "$tmp" /root/caves.tgz 2>/dev/null && { ok=1; break; }
    sleep 15
done
rm -f "$tmp"; [ $ok = 1 ] || { echo "$N: upload failed"; exit 1; }
timeout 1200 "$B" "$N" '( cd /root/caves && tar xzf ../caves.tgz && export PATH=/usr/local/cuda/bin:$PATH LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
(command -v g++ >/dev/null && command -v python3 >/dev/null) || (apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq g++ gcc python3 >/dev/null 2>&1)
for f in noise biomes layers biomenoise; do gcc -O3 -march=native -fwrapv -c -o cub_$f.o cubiomes/$f.c || echo "cubiomes $f FAILED"; done
g++ -O3 -march=native -std=c++17 -I cubiomes cavepairs3.cpp cub_*.o -lpthread -o cavepairs3 || echo "cavepairs3 FAILED"
g++ -O3 -march=native -std=c++17 -I cubiomes formulacave.cpp cub_*.o -lpthread -o formulacave || echo "formulacave FAILED"
g++ -O3 -march=native -std=c++17 -ffp-contract=off -I cubiomes native/cavemeasure.cpp cub_noise.o -lpthread -o native/cavemeasure || echo "cavemeasure FAILED"
nvcc -O3 -c gpuscan.cu -o gpuscan.o || echo "gpuscan FAILED"; nvcc -O3 -c gputables.cu -o gputables.o || echo "gputables FAILED"; nvcc -O3 -c gpulook.cu -o gpulook.o || echo "gpulook FAILED"
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate10.cpp gpuscan.o gputables.o cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o seedgate10 || echo "seedgate10 FAILED"
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate11.cpp gpuscan.o gputables.o gpulook.o cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o seedgate11 || echo "seedgate11 FAILED"
g++ -O3 -std=c++17 -I . -I cubiomes stats/scanbench.cpp gpuscan.o cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o scanbench || echo "scanbench FAILED"
g++ -O3 -march=native -std=c++17 -I . -I cubiomes stats/gpucheck.cpp gpuscan.o gpulook.o cub_noise.o -L/usr/local/cuda/lib64 -lcudart -lpthread -o gpucheck || echo "gpucheck FAILED"
command cp -f search3.py search3_new.py; chmod +x search3.py start_g13.sh
ls -la cavepairs3 formulacave native/cavemeasure seedgate10 seedgate11 scanbench | awk "{print \$5, \$9}"
nproc; cat /sys/fs/cgroup/cpu.max 2>/dev/null; cat /sys/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us /sys/fs/cgroup/cpu,cpuacct/cpu.cfs_period_us 2>/dev/null; grep -m1 "model name" /proc/cpuinfo; nvidia-smi --query-gpu=name,memory.total --format=csv,noheader; free -g | sed -n 2p ) 2>&1'
