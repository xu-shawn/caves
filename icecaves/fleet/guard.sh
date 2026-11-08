#!/bin/bash
# icecaves/fleet/guard.sh: the spending guard of the rented box(es) of the ice caves search (labels ice-*; no other
# instance is ever touched). Every 2 minutes: the spending since the mark fleet/state/spend_t0 (each ice-* instance's
# hourly price x its time up since the mark, so a sleeping laptop does not undercount) goes to fleet/state/spend; every
# 10 minutes the runs' result files are copied here (runs/RUN/ as for a local run). When the spending reaches CAP
# dollars the run is ended: the search is stopped on the box, runs/RUN is packed, fetched, checked by its sha1 and
# unpacked here, and only then is the instance destroyed (if the download fails the instance is left and KEPT is logged
# in fleet/events.log: look at it by hand).
# usage: nohup fleet/guard.sh CAP BOX RUN [RUN ...] > fleet/guard.log 2>&1 &      (FETCH_ONLY=1: copy the results now and leave)
cd "$(dirname "$0")/.." || exit 1
export FLEET_PREFIX=ice- VAST_SSH_KEY=${VAST_SSH_KEY:-$HOME/.ssh/vastai}
B=../../desert-search/deploy/box.sh; BOXES=../../desert-search/deploy/boxes.txt; CAP=$1; N=$2; shift 2; RUNS="$*"
sync_runs() { # the small files of every run, each into runs/RUN/ (replaced only by a complete copy)
    for r in $RUNS; do
        mkdir -p runs/$r
        for f in seeds.log results.txt gate.txt progress.txt settings.txt next_index; do
            timeout 300 $B "$N" get "/root/ice/icecaves/runs/$r/$f" "runs/$r/.$f.part" 2>/dev/null && command mv -f "runs/$r/.$f.part" "runs/$r/$f"
        done
    done
}
fetch_all() { # everything, checked; 0 if it came down whole
    local sum
    sum=$(timeout 600 $B "$N" "cd /root/ice/icecaves && tar czf /root/ice_runs.tgz --exclude='tmp_*' runs && sha1sum /root/ice_runs.tgz | cut -d' ' -f1" 2>/dev/null | tail -1)
    [ -n "$sum" ] || return 1
    for try in 1 2 3 4 5; do
        timeout 900 $B "$N" get /root/ice_runs.tgz fleet/state/ice_runs.tgz 2>/dev/null && [ "$(sha1sum fleet/state/ice_runs.tgz | cut -d' ' -f1)" = "$sum" ] && { tar xzf fleet/state/ice_runs.tgz && return 0; }
        sleep 10
    done
    return 1
}
if [ -n "$FETCH_ONLY" ]; then sync_runs; echo "copied: $(for r in $RUNS; do echo "$r $(wc -l < runs/$r/seeds.log 2>/dev/null) seeds"; done)"; exit; fi
T0=$(cat fleet/state/spend_t0); k=0
echo "$(date -u +%FT%TZ) guard: cap \$$CAP since $(date -u -d @$T0 +%FT%TZ), box $N, runs $RUNS"
while true; do
    spend=$(vastai show instances --raw 2>/dev/null | python3 -c "
import json, sys, time
t0 = $T0; now = time.time(); spend = 0.0
for i in json.load(sys.stdin):
    if str(i.get('label') or '').startswith('ice-'): spend += i.get('dph_total', 0) * (now - max(t0, i.get('start_date') or t0)) / 3600
print(f'{spend:.4f}')" 2>/dev/null)
    [ -n "$spend" ] && echo "$spend" >| fleet/state/spend
    if [ -n "$spend" ] && python3 -c "import sys; sys.exit(0 if float('$spend') >= $CAP else 1)"; then
        echo "STOP $(date -u +%FT%TZ) spending \$$spend reached the cap \$$CAP; downloading, then destroying ice-$N" >> fleet/events.log
        timeout 120 $B "$N" 'pkill -x gaterun.sh; pkill -x xargs; pkill -x icegate; sleep 1; pkill -x seed.sh; pkill -x icepairs; pkill -x iceformula; pkill -x icemeasure; true' 2>/dev/null
        ok=0; for try in 1 2 3; do fetch_all && { ok=1; break; }; sleep 20; done
        if [ $ok = 1 ]; then
            id=$(vastai show instances --raw | python3 -c "import json,sys; print(next((i['id'] for i in json.load(sys.stdin) if i.get('label') == 'ice-$N'), ''))")
            [ -n "$id" ] && vastai destroy instance "$id" -y && sed -i "/^$N /d" "$BOXES" && echo "DOWN $(date -u +%FT%TZ) results here, ice-$N ($id) destroyed" >> fleet/events.log
        else
            echo "KEPT $(date -u +%FT%TZ) ice-$N: download failed, instance left running" >> fleet/events.log
        fi
        exit
    fi
    k=$((k + 1)); [ $((k % 5)) = 0 ] && sync_runs
    sleep 120
done
