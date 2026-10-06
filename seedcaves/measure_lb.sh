#!/bin/bash
# Exact cavern volumes with the real game's terrain, LARGE BIOMES preset (seedcaves/java, compiled from the copies of
# ../java with NoiseGeneratorSettings.LARGE_BIOMES; -Dcave.normal=true gives the normal overworld again).
# usage: measure_lb.sh [THREADS] < "SEED X Y Z ..."      biome tools: measure_lb.sh -c BiomeProbe [strict|lenient] < "SEED X Y Z"
D="$(cd "$(dirname "$0")" && pwd)"; C="$(dirname "$D")"
J=${JAVA25:-$HOME/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java}
CLASS=CaveMeasure; if [ "$1" = "-c" ]; then CLASS=$2; shift 2; fi
if [ $CLASS = CaveMeasure ]; then set -- "${1:-6}"; fi
exec "$J" -Xmx${CAVE_HEAP:-5g} ${CAVE_NORMAL:+-Dcave.normal=true} -cp "$D/java/out:$(sed "s|mc/|$C/mc/|g" "$C/java/classpath.txt")" $CLASS "$@" 2> >(grep -v "^WARNING" >&2)
