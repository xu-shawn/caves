#!/usr/bin/env python3
# Distinct caverns of the snapshot lists, largest first.
# A cavern is counted once per (SEED, VOLUME); out: "RANK SEED TX TY TZ VOLUME CORE x0 x1 y0 y1 z0 z1 nlines sources"
import sys, os
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "snap")
seen = {}; bad = 0; total = 0
for name in ("cv1_s3", "local_probe_s3", "backfill_s3"):
    for line in open(os.path.join(d, name + ".txt")):
        w = line.split()
        if len(w) < 20 or w[11] != "at" or w[15] != "chunks" or w[17] != "seconds":
            bad += 1; continue
        try:
            seed = int(w[0]); vol = int(w[3]); core = int(w[4]); box = [int(v) for v in w[5:11]]; tp = [int(v) for v in w[12:15]]
        except ValueError:
            bad += 1; continue
        total += 1
        if vol <= 0: continue
        k = (seed, vol)
        if k in seen:
            e = seen[k]; e["n"] += 1; e["src"].add(name)
            if (e["core"], e["box"]) != (core, box): e["differs"] = True
        else: seen[k] = dict(seed=seed, vol=vol, core=core, box=box, tp=tp, n=1, src={name}, differs=False)
rows = sorted(seen.values(), key=lambda e: (-e["vol"], e["seed"]))
print(f"# lines {total} bad {bad} distinct {len(rows)}", file=sys.stderr)
for r, e in enumerate(rows, 1):
    print(r, e["seed"], *e["tp"], e["vol"], e["core"], *e["box"], e["n"], ",".join(sorted(e["src"])) + (",DIFFERS" if e["differs"] else ""))
