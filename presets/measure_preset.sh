#!/bin/bash
# Exact cavern volumes with the real game's terrain in another world type: the tool of ../measure.sh built from copies
# of ../java with the noise settings chosen by a property (presets/java).
# usage: presets/measure_preset.sh normal|large|amplified [THREADS=6] < "SEED X Y Z ..."
D="$(cd "$(dirname "$0")" && pwd)"; C="$(dirname "$D")"; P=${1:-normal}; T=${2:-6}
J=${JAVA25:-$HOME/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java}
H=320; [ "$P" = amplified ] && H=384
exec "$J" -Xmx${CAVE_HEAP:-6g} -Dcave.preset=$P -Dcave.height=$H -cp "$D/out:$(sed "s|mc/|$C/mc/|g" "$C/java/classpath.txt")" CaveMeasure "$T" 2>/dev/null
