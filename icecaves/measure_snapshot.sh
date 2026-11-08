#!/bin/bash
# Exact cavern volumes on the terrain 26.4-snapshot-3 itself makes (java/CaveMeasure.java, the cave project's tool moved
# to the snapshot's jar): measure_snapshot.sh [THREADS] < "SEED X Y Z ..."
D="$(cd "$(dirname "$0")" && pwd)"
J=${JAVA25:-$HOME/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java}
exec "$J" -Xmx${CAVE_HEAP:-3g} -cp "$D/java/out:$(sed "s|mc/|$D/mc/|g" "$D/java/classpath.txt")" CaveMeasure "${1:-8}" 2>/dev/null
