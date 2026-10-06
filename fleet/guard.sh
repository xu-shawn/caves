#!/bin/bash
# Guard for the cave search. It ends the run when either
#   - the spending on the cv-* boxes since the start mark (state/spend_t0, a unix time) reaches CAP dollars (default 1.0), or
#   - vast credit falls below FLOOR dollars (default 2.5).
# Ending the run: each cv* search is stopped, its hit files and logs go to caves/final/<box>/, and only then is the
# instance destroyed (a box whose download fails is left running). Other instances on the account are never touched.
# Events go to caves/events.log (RUN-STOP, RUN-KEPT, RUN-DOWN). Spending is the boxes' hourly price times the time
# since the mark, so a sleeping laptop doesn't undercount it.
# usage: nohup caves/fleet/guard.sh [CAP] [FLOOR] > caves/fleet/guard.log 2>&1 &
cd "$(dirname "$0")/.." || exit 1
export FLEET_PREFIX=cv- VAST_SSH_KEY=$HOME/.ssh/vastai
B=../desert-search/deploy/box.sh; BOXES=../desert-search/deploy/boxes.txt; CAP=${1:-1.0}; FLOOR=${2:-2.5}
T0=$(cat state/spend_t0)
echo "$(date -u +%FT%TZ) guard: cap \$$CAP since $(date -u -d @$T0 +%FT%TZ), credit floor \$$FLOOR"
while true; do
    reason=""
    info=$(vastai show instances --raw 2>/dev/null | python3 -c "
import json, sys, time
t0 = $T0; now = time.time(); spend = 0.0
for i in json.load(sys.stdin):
    if str(i.get('label') or '').startswith('cv-'): spend += i.get('dph_total', 0) * (now - max(t0, i.get('start_date') or t0)) / 3600
print(f'{spend:.3f}')" 2>/dev/null)
    [ -n "$info" ] && echo "$info" >| state/spend
    credit=$(vastai show user --raw 2>/dev/null | python3 -c "import json,sys; print(json.load(sys.stdin).get('credit', ''))" 2>/dev/null)
    if [ -n "$info" ] && python3 -c "import sys; sys.exit(0 if float('$info') >= $CAP else 1)"; then reason="spending \$$info reached the cap \$$CAP"
    elif [ -n "$credit" ] && python3 -c "import sys; sys.exit(0 if float('$credit') < $FLOOR else 1)"; then reason="credit \$$credit below \$$FLOOR"; fi
    if [ -n "$reason" ]; then
        echo "RUN-STOP $(date -u +%FT%TZ) $reason; downloading results, then destroying the cv boxes" >> events.log
        for n in $(awk '$1 ~ /^cv[0-9]/ {print $1}' "$BOXES"); do
            mkdir -p final/$n; ok=0
            for try in 1 2 3; do
                timeout 180 $B "$n" 'cd /root/caves; pkill -x python3; pkill -x cavepairs; pkill -x formulacave; pkill -x cavemeasure; sleep 2; tar czf - runs 2>/dev/null' >| final/$n/results.tgz 2>/dev/null && tar xzf final/$n/results.tgz -C final/$n 2>/dev/null && { ok=1; break; }
                sleep 10
            done
            if [ $ok = 1 ]; then
                id=$(vastai show instances --raw | python3 -c "import json,sys; print(next((i['id'] for i in json.load(sys.stdin) if i.get('label') == 'cv-$n'), ''))")
                [ -n "$id" ] && vastai destroy instance "$id" -y && sed -i "/^$n /d" "$BOXES" && echo "$(date -u +%FT%TZ) destroyed $n ($id)"
            else
                echo "RUN-KEPT $(date -u +%FT%TZ) $n: download failed, instance left running" >> events.log
            fi
        done
        echo "RUN-DOWN $(date -u +%FT%TZ) run ended ($reason)" >> events.log
        exit 0
    fi
    sleep 120
done
