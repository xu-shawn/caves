#!/bin/bash
# Uploads the cave search as it stands and builds it in /root/caves on a rented box with a CUDA card:
#   the gate (seedgate14: GPU phase scan along the chain with the terrain score, coarse looks on CPU threads and on the GPU with the z filter made once per layer, first look, full gate; seedgate13 beside it),
#   gpusrv, the GPU's services for the workers (the scanner's terrain step, its tables and formula step, the formula
#   stage, the exact measurer's corner densities), and the workers' programs that use them (cavepairs5, formulacave5,
#   native/cavemeasure), the driver, cubiomes.
# Then on the box: nvidia-cuda-mps-control -d (so that gate and gpusrv share the card), ./start_g13.sh.
# usage: setup5.sh NAME [ARCH=sm_89]     (ARCH: the card's architecture for nvcc: sm_89 RTX 40, sm_86 RTX 30 / A2000, sm_75 RTX 20)
export FLEET_PREFIX=cv- VAST_SSH_KEY=${VAST_SSH_KEY:-$HOME/.ssh/vastai}
L="$(cd "$(dirname "$0")/../.." && pwd)"; B="$L/desert-search/deploy/box.sh"; N=$1; ARCH=${2:-sm_89}
tmp=$(mktemp --suffix=.tgz)
tar czf "$tmp" -C "$L/caves" cavepairs3.cpp cavepairs5.cpp formulacave.cpp formulacave5.cpp seedgate10.cpp seedgate12.cpp seedgate13.cpp seedgate14.cpp cpuprelook2.hpp gpulook2.cu cavenoise.hpp cpuprelook.hpp tableseed.hpp analyticgate.hpp percgate.hpp name_hashes.h noise_params.h \
    gpuscan.cu gpuscan.h gputables.cu gpulook.cu gputerrain.cu gputerrain.h gpuformula.cu gpupairs.cu terrainseed.hpp gpusrv.cpp search3.py stats/scanbench.cpp stats/lookcheck.cpp stats/lookcheck2.cpp stats/gpucheck.cpp stats/tablecheck.cpp tests/caverns13.txt tests/caverns13.expected tests/check_measure.sh \
    native/density_gen.hpp native/gamegen_types.hpp native/gamegen.hpp native/cavemeasure.cpp native/gamegen_dev.hpp native/gen_density_dev.py native/gpudensity.cu native/gpudensity.h native/densitypack.hpp native/densitycheck.cpp \
    -C "$L/caves/fleet/box" start_g13.sh start_g14.sh rate.sh -C "$L/caves/vendor" cubiomes || exit 1
ok=0
for try in 1 2 3 4 5 6; do
    timeout 60 "$B" "$N" 'mkdir -p /root/caves' 2>/dev/null && timeout 180 "$B" "$N" put "$tmp" /root/caves.tgz 2>/dev/null && { ok=1; break; }
    sleep 15
done
rm -f "$tmp"; [ $ok = 1 ] || { echo "$N: upload failed"; exit 1; }
timeout 1800 "$B" "$N" '( cd /root/caves && tar xzf ../caves.tgz && export PATH=/usr/local/cuda/bin:$PATH LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH ARCH='"$ARCH"'
(command -v g++ >/dev/null && command -v python3 >/dev/null) || (apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq g++ gcc python3 >/dev/null 2>&1)
C="-L/usr/local/cuda/lib64 -lcudart -lpthread"; fail() { echo "$1 FAILED"; }
for f in noise biomes layers biomenoise; do gcc -O3 -march=native -fwrapv -c -o cub_$f.o cubiomes/$f.c || fail "cubiomes $f"; done
nvcc -O3 -c gpuscan.cu -o gpuscan.o || fail gpuscan   # NOT -arch=sm_89: the scan kernel built for it runs at a fifth of the speed (1.8e10 against 8.2e10 seeds/s on a 4080 Super)
for f in gputables gpulook gpulook2 gputerrain gpuformula gpupairs; do nvcc -O3 -arch=$ARCH -c $f.cu -o $f.o || fail $f; done
(cd native && python3 gen_density_dev.py > /dev/null && nvcc -O3 --fmad=false -arch=$ARCH -c gpudensity.cu -o gpudensity.o) || fail gpudensity
g++ -O3 -march=native -std=c++17 -I cubiomes cavepairs5.cpp cub_*.o -lpthread -o cavepairs5 || fail cavepairs5
g++ -O3 -march=native -std=c++17 -I cubiomes cavepairs3.cpp cub_*.o -lpthread -o cavepairs3 || fail cavepairs3
g++ -O3 -march=native -std=c++17 -I cubiomes formulacave5.cpp cub_*.o -lpthread -o formulacave || fail formulacave
g++ -O3 -march=native -std=c++17 -ffp-contract=off -I cubiomes native/cavemeasure.cpp cub_noise.o -lpthread -o native/cavemeasure || fail cavemeasure
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate12.cpp gpuscan.o gputables.o gpulook.o cub_noise.o $C -o seedgate12 || fail seedgate12
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate13.cpp gpuscan.o gputables.o gpulook.o cub_noise.o $C -o seedgate13 || fail seedgate13
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate14.cpp gpuscan.o gputables.o gpulook2.o cub_noise.o $C -o seedgate14 || fail seedgate14
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate14.cpp gpuscan.o gputables.o gpulook.o cub_noise.o $C -o seedgate14_oldlook || fail seedgate14_oldlook
g++ -O3 -march=native -std=c++17 -I . -I cubiomes stats/lookcheck2.cpp cub_noise.o -o lookcheck2 || fail lookcheck2
g++ -O3 -march=native -std=c++17 -I . -I cubiomes stats/tablecheck.cpp gputables.o cub_noise.o $C -o tablecheck || fail tablecheck
g++ -O3 -march=native -std=c++17 -I . -I cubiomes stats/gpucheck.cpp gpuscan.o gpulook2.o cub_noise.o $C -o gpucheck2 || fail gpucheck2
g++ -O3 -march=native -std=c++17 -I cubiomes seedgate10.cpp gpuscan.o gputables.o cub_noise.o $C -o seedgate10 || fail seedgate10
g++ -O3 -march=native -std=c++17 -ffp-contract=off -I cubiomes gpusrv.cpp gputerrain.o gpuformula.o gpupairs.o native/gpudensity.o cub_*.o $C -o gpusrv 2>&1 | grep -E " error " ; [ -x gpusrv ] || fail gpusrv
(cd native && g++ -O3 -march=native -std=c++17 -ffp-contract=off -I ../cubiomes densitycheck.cpp gpudensity.o ../cub_noise.o $C -o densitycheck) || fail densitycheck
g++ -O3 -march=native -std=c++17 -I . -I cubiomes stats/gpucheck.cpp gpuscan.o gpulook.o cub_noise.o $C -o gpucheck || fail gpucheck
command cp -f search3.py search3_new.py; chmod +x search3.py start_g13.sh start_g14.sh rate.sh tests/check_measure.sh
ls -la cavepairs5 formulacave native/cavemeasure seedgate12 seedgate13 seedgate14 lookcheck2 gpucheck2 gpusrv native/densitycheck gpucheck | awk "{print \$5, \$9}"
nproc; cat /sys/fs/cgroup/cpu.max 2>/dev/null; cat /sys/fs/cgroup/cpu,cpuacct/cpu.cfs_quota_us /sys/fs/cgroup/cpu,cpuacct/cpu.cfs_period_us 2>/dev/null; grep -m1 "model name" /proc/cpuinfo; nvidia-smi --query-gpu=name,memory.total --format=csv,noheader
echo "checks: ./gpucheck terrain; ./gpucheck look; native/densitycheck SEED X Z   docs/TESTING.md has the procedures. GPU_DENSITY=1 tests/check_measure.sh (the exact measurer against its 13 reference caverns, once gpusrv runs); ./lookcheck2 3000 (the CPU looks, old against new); ./gpucheck2 look (the new GPU look against the CPU); ./tablecheck 512 < seeds (the first look against the CPU; GPUTABLES_OLD=1 for the kernel before)   then: nvidia-cuda-mps-control -d; CAVE_AHEAD=2 CPU_LOOKS=17 GATE_THREADS=31 PER_THREAD=512 WORKERS=46 PRE=-0.472 ./start_g14.sh; ./rate.sh 240 (the rates, to judge a setting by)" ) 2>&1'
