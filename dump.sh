#!/bin/bash
# Real-game terrain dump of a block region (see java/CaveDump.java): dump.sh SEED X0 Z0 SIZE_X SIZE_Z OUT [THREADS]
D="$(cd "$(dirname "$0")" && pwd)"
J=${JAVA25:-$HOME/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java}
exec "$J" -Xmx4g -cp "$D/java/out:$(sed "s|mc/|$D/mc/|g" "$D/java/classpath.txt")" CaveDump "$@" 2>&1 | grep -v "^WARNING"
