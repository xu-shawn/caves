#!/bin/bash
# Exact cavern volumes by flood fill with the real game's terrain (java/CaveMeasure.java): measure.sh [THREADS] < "SEED X Y Z ..."
D="$(cd "$(dirname "$0")" && pwd)"
J=${JAVA25:-$HOME/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java}
exec "$J" -Xmx${CAVE_HEAP:-5g} -cp "$D/java/out:$(sed "s|mc/|$D/mc/|g" "$D/java/classpath.txt")" CaveMeasure "${1:-11}" 2>/dev/null
