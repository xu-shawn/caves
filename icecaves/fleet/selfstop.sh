#!/bin/bash
# icecaves/fleet/selfstop.sh (on the box): the backup of the laptop's guard. At DEADLINE (unix time) the search is
# stopped and the instance stops itself with its own key (the results stay on its disk for the laptop to fetch).
# usage: selfstop.sh DEADLINE      (selfstop.sh check: only asks the API for this instance, to see that the key works)
eval $(tr '\0' '\n' < /proc/1/environ | grep -E '^(CONTAINER_API_KEY|CONTAINER_ID)=' | sed 's/^/export /')
call() { python3 - "$1" <<'PY'
import json, os, sys, urllib.request
url = f"https://console.vast.ai/api/v0/instances/{os.environ['CONTAINER_ID']}/"; head = {"Authorization": "Bearer " + os.environ["CONTAINER_API_KEY"], "Content-Type": "application/json"}
req = urllib.request.Request(url, headers=head) if sys.argv[1] == "check" else urllib.request.Request(url, data=json.dumps({"state": "stopped"}).encode(), method="PUT", headers=head)
try: print(urllib.request.urlopen(req, timeout=60).read().decode()[:300])
except Exception as e: print("failed:", e)
PY
}
[ "$1" = check ] && { call check; exit; }
while [ $(date +%s) -lt $1 ]; do sleep 60; done
pkill -x gaterun.sh; pkill -x xargs; pkill -x icegate; sleep 2
for try in 1 2 3 4 5; do date -u >> /root/selfstop.log; call stop >> /root/selfstop.log 2>&1; sleep 120; done
