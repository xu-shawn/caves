#!/bin/bash
# icecaves/fleet/start.sh (on the box): the search, the gate on the card feeding seed.sh on the cores.
# usage: fleet/start.sh NAME SPEC_FILE [JOBS=nproc-1] [COUNT=4000000000000] [FIRST=1000000000000000]
#   env as for seed.sh (the settings of the run); the run ends with `touch runs/NAME/STOP` (after its chunk) or stop.sh.
cd "$(dirname "$0")/.." || exit 1
export PATH=/usr/local/cuda/bin:$PATH LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
N=$1; S=$2; J=${3:-$(( $(nproc) - 1 ))}; C=${4:-4000000000000}; F=${5:-1000000000000000}
mkdir -p runs/$N; env | grep -E '^(SELECT|PAIRS|WBOX_MIN|WBOX_SLOPE|OWN_SIZE|PLACES|PLACES_ICE|TOP|NP|SP_MIN|NEAR_PCT|PURE_MIN|MP|MI|NI|SI_MIN|ICE_MIN|FORMULA_BY|INDEX|SCANNER)=' | sort > runs/$N/settings.txt
GATE_GPU=1 MAX_CHUNKS=100000 setsid nohup ./gaterun.sh $N $S 0 $J $C $F > runs/$N/gaterun.log 2>&1 < /dev/null &
echo "started $N: $J workers, chunks of $C from $(cat runs/$N/next_index 2>/dev/null || echo $F)"
