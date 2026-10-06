#!/bin/bash
# Build docs/PROGRESSION.pdf from docs/PROGRESSION.md (needs pandoc and chromium). The table of the largest caverns
# (TOP_TABLE in the text) comes from hits/*.txt, and the rarity figure from stats/fit_exact.png.
set -e; cd "$(dirname "$0")"
command cp -f ../stats/fit_exact.png img/fit_exact.png; command cp -f ../stats/fit_dollar.png img/fit_dollar.png
python3 build_rates.py
python3 - <<'PY'
rows = {}
for f in ('cv1_s3.txt', 'cv2_s3.txt', 'cv3_s3.txt', 'local_probe_s3.txt', 'backfill_s3.txt', 'local_top_s3.txt'):
    for l in open('../hits/' + f):
        w = l.split()
        if len(w) > 14 and float(w[3]) > 0: rows[(w[0], w[3])] = w
top = sorted(rows.values(), key=lambda w: -int(w[3]))[:14]
table = '| # | blocks | seed | /tp | size x, y, z |\n|---|-----------|----------------|--------------------------|------------------|\n' + ''.join(f'| {i + 1} | {int(w[3]):,} | {w[0]} | {w[12]} {w[13]} {w[14]} | {int(w[6]) - int(w[5]) + 1} × {int(w[8]) - int(w[7]) + 1} × {int(w[10]) - int(w[9]) + 1} |\n' for i, w in enumerate(top))
rates = open('rate_table.md').read()   # written by build_rates.py from stats/fit_exact.py's counts
dollars = open('../stats/dollar_table.md').read()   # written by stats/fit_dollar.py
import json; last = json.load(open('../stats/dollar_last.json'))   # the last build's numbers for the text, written by stats/fit_dollar.py
open('PROGRESSION_built.md', 'w').write(open('PROGRESSION.md').read().replace('TOP_TABLE', table).replace('RATE_TABLE', rates).replace('DOLLAR_TABLE', dollars).replace('PER_DOLLAR_FACTOR', last['factor']).replace('PER_DOLLAR_RATE', last['rate']).replace('PER_DOLLAR_SHIFT', last['shift']).replace('PER_DOLLAR_46', last['r46']).replace('PER_DOLLAR_N46', last['n46']))
PY
pandoc PROGRESSION_built.md -f markdown+pipe_tables+implicit_figures -t html5 --standalone --wrap=none \
  --metadata pagetitle="$(head -1 PROGRESSION.md | sed 's/^# //')" --css progression.css -o PROGRESSION.html
chromium --headless=new --disable-gpu --no-pdf-header-footer --virtual-time-budget=10000 \
  --print-to-pdf="$PWD/PROGRESSION.pdf" "file://$PWD/PROGRESSION.html" 2>/dev/null
rm -f PROGRESSION.html PROGRESSION_built.md
echo "wrote $PWD/PROGRESSION.pdf"
