#!/bin/bash
# Watches the cave search on the cv* boxes: mirrors every exact measurement (runs/*/s3.txt) to caves/hits/<box>_s3.txt and
# logs to caves/events.log
#   BEST <blocks> seed=... tp=x,y,z box=...     a new largest exact cavern (over the boxes and the start value below)
#   DOWN <box> / SSH-FAIL <box>                 no search process / unreachable for 5 polls
# usage: nohup caves/watch_cv.sh [BEST_START] > /dev/null 2>&1 &
cd "$(dirname "$0")" || exit 1
export FLEET_PREFIX=cv- VAST_SSH_KEY=$HOME/.ssh/vastai
B=../desert-search/deploy/box.sh; BOXES=../desert-search/deploy/boxes.txt
mkdir -p hits state; touch events.log
[ -f state/best ] || echo "${1:-20190312}" > state/best
REMOTE='cd /root/caves 2>/dev/null || { echo NOTBUILT; exit 0; }; if pgrep -x python3 > /dev/null; then echo ALIVE; else echo DEAD; fi; echo "$(cat runs/*/seeds.txt 2>/dev/null | wc -l) seeds"; cat runs/*/s3.txt 2>/dev/null'
while true; do
    for n in $(awk '$1 ~ /^cv[0-9]/ && NF >= 3 {print $1}' "$BOXES" 2>/dev/null); do
        if out=$(timeout 60 $B "$n" "$REMOTE" 2>/dev/null) && [ -n "$out" ]; then
            echo 0 > state/$n.fails
            case "$(echo "$out" | head -1)" in
                DEAD) [ -f state/$n.down ] || { echo "DOWN $n $(date -u +%FT%TZ) no search process" >> events.log; touch state/$n.down; } ;;
                ALIVE) rm -f state/$n.down ;;
            esac
            echo "$out" | sed -n 2p > state/$n.progress
            echo "$out" | tail -n +3 > hits/${n}_s3.txt.tmp && command mv -f hits/${n}_s3.txt.tmp hits/${n}_s3.txt
            top=$(sort -k4,4nr hits/${n}_s3.txt | head -1); best=$(cat state/best)
            if [ -n "$top" ] && [ "$(echo "$top" | awk '{print $4}')" -gt "$best" ]; then
                echo "$top" | awk '{print $4}' > state/best
                echo "BEST $(echo "$top" | awk '{print $4, "seed=" $1, "x=" $6 ".." $7, "y=" $8 ".." $9, "z=" $10 ".." $11, "tp=" $13 "," $14 "," $15}') box=$n $(date -u +%FT%TZ)" >> events.log
            fi
        else
            f=$(( $(cat state/$n.fails 2>/dev/null || echo 0) + 1 )); echo $f > state/$n.fails
            [ $f = 5 ] && echo "SSH-FAIL $n $(date -u +%FT%TZ) unreachable for 5 polls" >> events.log
        fi
    done
    sleep 120
done
