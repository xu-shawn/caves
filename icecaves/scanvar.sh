#!/bin/bash
# icecaves/scanvar.sh: the scanner alone on a list of seeds under a setting, to compare settings on the same seeds.
# usage: scanvar.sh NAME SEEDS_FILE JOBS [PAIRS=1000000] [PLACES=700] [VAR=value ...]     (the VARs go to icepairs' environment)
# writes runs/scanvar/NAME.txt: per seed "SEED seconds n300 n400 n500 best" (places whose fine score, the pure cluster or
# the ice cluster with at most 8% outside, is at least 300 / 400 / 500 points; overlapping windows once) and prints sums.
# BIN=icepairs2 uses another build of the scanner.
D="$(cd "$(dirname "$0")" && pwd)"; N=$1; F=$2; J=$3; P=${4:-1000000}; L=${5:-700}; shift 5 2>/dev/null; mkdir -p "$D/runs/scanvar"; : > "$D/runs/scanvar/$N.txt"
one() { t0=$(date +%s.%N); env "${@:4}" "$D/${BIN:-icepairs}" $1 $2 80 1 $3 2>/dev/null | awk -v seed=$1 -v t0=$t0 '{ s = $9; if ($5 > s && ($7 - $5) * 100 <= 8 * $7) s = $5; if (s >= 300) a++; if (s >= 400) b++; if (s >= 500) c++; if (s > m) m = s }
    END { "date +%s.%N" | getline t1; printf "%s %.1f %d %d %d %d\n", seed, t1 - t0, a, b, c, m }'; }
export -f one; export D BIN
xargs -P $J -I{} bash -c 'one {} '"$P $L $*" < "$F" >> "$D/runs/scanvar/$N.txt"
awk -v name=$N '{t += $2; a += $3; b += $4; c += $5; m += $6; n++} END {printf "%s: %d seeds, %.1f s a seed; places a seed with score 300+ %.2f, 400+ %.2f, 500+ %.3f; mean best %.0f\n", name, n, t / n, a / n, b / n, c / n, m / n}' "$D/runs/scanvar/$N.txt"
