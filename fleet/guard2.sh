#!/bin/bash
# Guard for the cave search (second version: the results come down in checked 2 MB parts). It ends the run when either
#   - the spending on the cv-* boxes since the start mark (state/spend_t0, a unix time) reaches CAP dollars (default 1.0), or
#   - vast credit falls below FLOOR dollars (default 2.5).
# Ending the run: each cv* search is stopped; its results (everything in /root/caves/runs and /root/caves/phasecal but the
# claim files and the workers' output logs, with the formula-site lists cut to 19M+) are packed on the box, fetched part
# by part (each retried, the whole checked by its sha1), unpacked into caves/final/<box>/, and only then is the instance
# destroyed (a box whose download fails is left running and RUN-KEPT is logged). Other instances are never touched.
# Events go to caves/events.log (RUN-STOP, RUN-KEPT, RUN-DOWN). Spending is the boxes' hourly price times the time
# since the mark, so a sleeping laptop doesn't undercount it.
# usage: nohup caves/fleet/guard2.sh [CAP] [FLOOR] > caves/fleet/guard.log 2>&1 &        (FETCH_ONLY=name: fetch that box now, no stop, no destroy)
cd "$(dirname "$0")/.." || exit 1
export FLEET_PREFIX=cv- VAST_SSH_KEY=$HOME/.ssh/vastai
B=../desert-search/deploy/box.sh; BOXES=../desert-search/deploy/boxes.txt; CAP=${1:-1.0}; FLOOR=${2:-2.5}
PACK='cd /root/caves && rm -rf /root/final && mkdir -p /root/final && for f in runs/*/s2.txt; do awk -F"|" "{split(\$2, a, \" \"); if (a[1] >= 19000000) print}" $f > ${f%.txt}_19M.txt; done; tar cJf /root/final/all.txz --exclude=claims --exclude="search*.out" --exclude=s2.txt runs phasecal 2>/dev/null; cd /root/final && split -b 2M -d -a 4 all.txz part. && echo "$(ls part.* | wc -l) $(sha1sum all.txz | cut -d" " -f1)"'
fetch() { # box name -> final/<name>/; 0 if everything came down and unpacked
    local n=$1 info parts sum i ok
    mkdir -p final/$n/parts
    info=$(timeout 900 $B "$n" "$PACK" 2>/dev/null | tail -1); parts=${info%% *}; sum=${info##* }
    [ -n "$parts" ] && [ "$parts" -gt 0 ] 2>/dev/null || return 1
    for i in $(seq 0 $((parts - 1))); do
        f=$(printf 'part.%04d' $i); ok=0
        for try in 1 2 3 4 5 6 7 8; do
            timeout 120 $B "$n" "base64 -w0 /root/final/$f" 2>/dev/null | base64 -d >| final/$n/parts/$f 2>/dev/null
            [ "$(timeout 60 $B "$n" "sha1sum /root/final/$f" 2>/dev/null | cut -d' ' -f1)" = "$(sha1sum final/$n/parts/$f | cut -d' ' -f1)" ] && { ok=1; break; }
            sleep 5
        done
        [ $ok = 1 ] || return 1
    done
    cat final/$n/parts/part.* >| final/$n/all.txz
    [ "$(sha1sum final/$n/all.txz | cut -d' ' -f1)" = "$sum" ] || return 1
    tar xJf final/$n/all.txz -C final/$n && command rm -rf final/$n/parts
}
if [ -n "$FETCH_ONLY" ]; then fetch "$FETCH_ONLY" && echo "fetched $FETCH_ONLY: $(du -sh final/$FETCH_ONLY | cut -f1)" || echo "fetch of $FETCH_ONLY failed"; exit; fi
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
            timeout 120 $B "$n" 'pkill -x python3; pkill -x seedgate3; pkill -x seedgate2; pkill -x seedgate; pkill -x cavepairs3; pkill -x cavepairs; pkill -x formulacave; pkill -x cavemeasure; sleep 2; true' 2>/dev/null
            ok=0
            for try in 1 2 3; do fetch "$n" && { ok=1; break; }; sleep 20; done
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
