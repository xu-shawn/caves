#!/bin/bash
# icecaves/fleet/watch.sh: waits until something needs a look: a cavern that is ice caves throughout larger than
# BEST in the runs given (copies of the box's runs are refreshed by guard.sh), the guard gone, or MINUTES passed.
# usage: fleet/watch.sh BEST MINUTES RUN [RUN ...]
cd "$(dirname "$0")/.." || exit 1; BEST=$1; MIN=$2; shift 2; t0=$(date +%s)
while true; do
    top=$(cat $(for r in "$@"; do echo runs/$r/results.txt; done) 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "seconds") {if ($(i + 3) == $4 && $(i + 4) != "left" && $(i + 5) != "left" && $4 > m) {m = $4; s = $1} break}} END {print m + 0, s}')
    seeds=$(cat $(for r in "$@"; do echo runs/$r/seeds.log; done) 2>/dev/null | wc -l)
    if [ "${top%% *}" -gt "$BEST" ]; then echo "$(date -u +%T) NEW BEST $top ($seeds seeds, spend $(cat fleet/state/spend))"; exit 0; fi
    ps -eo args | grep -q '[f]leet/guard.sh' || { echo "$(date -u +%T) the guard is gone ($seeds seeds, best $top, spend $(cat fleet/state/spend)); $(tail -n 2 fleet/events.log 2>/dev/null)"; exit 0; }
    [ $(( $(date +%s) - t0 )) -ge $((MIN * 60)) ] && { echo "$(date -u +%T) $MIN minutes: $seeds seeds, best $top, spend $(cat fleet/state/spend)"; exit 0; }
    sleep 120
done
