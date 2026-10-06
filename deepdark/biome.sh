#!/bin/bash
# The real game's biomes (java/BiomeProbe.java, java/BiomeCells.java): pinned to 2 processors, small heap, lowest priority.
# biome.sh BiomeProbe < lines "SEED X Y Z"        biome.sh BiomeCells SEED FILE [uncached]
D="$(cd "$(dirname "$0")" && pwd)"; C="$(dirname "$D")"
J=${JAVA25:-$HOME/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java}
exec nice -n 19 taskset -c ${DD_CPUS:-10,11} "$J" -Xmx${DD_HEAP:-700m} -XX:ActiveProcessorCount=2 -XX:+UseSerialGC -cp "$D/java/out:$(sed "s|mc/|$C/mc/|g" "$C/java/classpath.txt")" "$@" 2> >(grep -v "^WARNING" >&2)
