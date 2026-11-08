#!/bin/bash
# icecaves/fleet/rate.sh (on the box): where a run stands. usage: fleet/rate.sh NAME
cd "$(dirname "$0")/.." || exit 1; N=$1; O=runs/$N
echo "$(wc -l < $O/seeds.log) seeds done; chunk: $(wc -l < $O/chunk.txt) seeds; next index $(cat $O/next_index); gate: $(tail -n 1 $O/gate.log | cut -c1-120)"
tail -n 2 $O/progress.txt 2>/dev/null
awk '{for (i = 1; i <= NF; i++) if ($i == "seconds") {v = $4; ok = ($(i + 3) == v && $(i + 4) != "left" && $(i + 5) != "left"); if (ok) {n++; if (v >= 3000000) a++; if (v >= 3500000) b++; if (v >= 4000000) c++; if (v >= 5000000) d++; if (v > m) m = v}}} END {printf "ice caves throughout: %d caverns; 3M+ %d, 3.5M+ %d, 4M+ %d, 5M+ %d; largest %d\n", n, a, b, c, d, m}' $O/results.txt 2>/dev/null
awk 'NF >= 30 {n++; p += $3; f += $5; m += $7} END {if (n) printf "thread-s a seed: scanner %.1f, formula %.1f, measure %.1f\n", p / n, f / n, m / n}' $O/seeds.log; uptime | sed 's/.*load/load/'
