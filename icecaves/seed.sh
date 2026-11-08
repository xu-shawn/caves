#!/bin/bash
# icecaves/seed.sh: the ice caverns of one seed, start to end, on one thread.
#   1. icepairs     PAIRS cave-noise alignments on windows of SELECT steps (of 16, 12, 10 and 8 the smallest finds the
#                   most); of those only the pairs with a place whose box weirdness is WBOX_MIN or more go on (the whole
#                   weirdness of a place is table lookups: its three octaves repeat with one or two periods of the cave
#                   noise, so the test costs a pair about a nanosecond and the cave noise can be taken 16x deeper);
#                   the rest of the climate at those places; a coarse first look at every one under ice caves; a fine
#                   look at the best PLACES of them by the fitted index (index_coarse.txt) and PLACES_ICE by ice points
#   2. iceformula   the NP best by size with a small charge for points outside the ice caves (ln(score) - 0.04 x percent
#                   outside), from SP_MIN points: 288 blocks around the cluster's middle, lattice of 32
#   3. icemeasure   the exact cavern, strict (given up as soon as it leaves the ice caves): the MP largest pure clusters
#                   of the formula, from PURE_MIN samples, and the MN largest nearly pure ones (below); in full: the MI
#                   best by ice caves samples, from ICE_MIN
# usage: seed.sh SEED OUT_DIR
#   env SELECT=6 PAIRS=32000000 WBOX_MIN=0.40 PLACES=1300 PLACES_ICE=200 TOP=200 INDEX=index_coarse.txt
#       NP=48 SP_MIN=220 NEAR_PCT=8 FORMULA_BY=size   NI=2 SI_MIN=800   MP=4 PURE_MIN=500   MN=5 NEAR_MIN=600 NEAR_OUT=2   MI=1 ICE_MIN=1000
#   SP_MIN, SI_MIN: fine points (about 8,400 blocks each) of the scanner's clusters; PURE_MIN, ICE_MIN: samples (8,192
#   blocks) of the formula's. A cavern has about 6,100 blocks a sample of the formula's pure cluster.
#   Measured on the same 200 gate seeds with PURE_MIN=350 (caverns that are ice caves throughout of 2M+ / 2.5M+ / 3M+ /
#   3.5M+; laptop thread-seconds a seed):
#     1M pairs, half kept by their own weirdness, windows of 12 and 16 (before)     22 /   6 /  3 /  2      8.1
#     16M pairs, box weirdness 0.35+, windows of 12 and 16                          98 /  54 / 16 /  2      9.6
#     the same, windows of 12 only                                                 146 /  76 / 25 /  5     11.5
#     windows of 10                                                                184 / 102 / 37 / 12     12.9
#     windows of 8                                                                 281 / 172 / 67 / 16     14.6
#   and on 651 gate seeds on a rented box (thread-seconds of an i9-14900KF, 31 jobs):
#     windows of 10, 16M pairs, box weirdness 0.35+                                653 / 348 / 106 / 27    10.0
#     windows of 8                                                                 885 / 551 / 203 / 46    12.2
#     windows of 6                                                                1166 / 807 / 292 / 66    13.4
#     windows of 4                                                                1223 / 921 / 307 / 55    14.0
#     windows of 6, 32M pairs at 0.40+ / 64M at 0.45+                 1184 / 835 / 316 / 64,  1245 / 846 / 304 / 58   13.6, 13.0
#   PURE_MIN: clusters of 350-450 samples are three quarters of the strict tests and held none of 16 caverns of 3.5M+
#   and 20 of 92 of 3M+ (PURE_MIN=350: every cavern from about 2.5M; 450: complete from 3.5M). The default 500 is for
#   the largest only: on 13,000 seeds clusters of 450-500 samples were 60% of these tests and their largest cavern
#   was 4.1M, while every cavern of 4.6M+ had 538 samples or more.
#   ICE_MIN: on fresh gated seeds 1,001 full measures from 780 samples gave nothing over 10.7M blocks of ice caves (the
#   leaders, from the cave gate's seeds, have 12.4M) and under 1,000 samples nothing over 9.6M.
#   The pipeline as it was before: WBOX_MIN= PAIRS=1000000 SELECT=12,16 PLACES=600 PLACES_ICE=100 TOP=100
#   FORMULA_BY=score PURE_MIN=350 ICE_MIN=780 (then OWN_KEEP=0.5: half the pairs, by their own weirdness).
#   What the formula's pure clusters of 230+ samples did in the game: ice caves throughout for 81% of those with no
#   edge, 55% with an edge under 2% of their size, 50% up to 4%, 30% beyond.
# Appends to OUT_DIR/results.txt (icemeasure lines), OUT_DIR/formula.txt (iceformula lines, unless KEEP_FORMULA=0) and
# OUT_DIR/seeds.log (a line a seed: times and bests). A line's tail carries what the earlier steps said of the place:
#   after the formula:  P|I  scanner pure, ice, of, edge
#   after the measure:  S|F  P|I  scanner pure, ice, of, edge  formula ice, of, pure, pure's edge
export LC_ALL=C   # (sort's order of equal keys depends on the locale: the same candidates on every machine)
D="$(cd "$(dirname "$0")" && pwd)"; S=$1; O=$2; W="$O/tmp_$S"; mkdir -p "$W"; t0=$(date +%s.%N)
[ -n "${INDEX+x}" ] || { [ -f "$D/index_coarse.txt" ] && export INDEX="$D/index_coarse.txt"; }   # the fitted ranking of places for the fine look (placefit.py, indexfit.py); INDEX= (empty) for the old two lists
[ -n "$INDEX" ] || unset INDEX
export PLACES_ICE=${PLACES_ICE:-200} SELECT=${SELECT:-6}
if [ -n "${WBOX_MIN-0.40}" ]; then export WBOX_MIN=${WBOX_MIN-0.40}; else unset WBOX_MIN; export OWN_KEEP=${OWN_KEEP:-0.5}; fi   # (WBOX_MIN= : the pruning before it, by the pairs' own weirdness)
"$D/${SCANNER:-icepairs}" $S ${PAIRS:-32000000} ${TOP:-200} 1 ${PLACES:-1300} > "$W/pairs.txt" 2> "$W/pairs.log" || { echo "$S icepairs failed" >> "$O/seeds.log"; exit 1; }
t1=$(date +%s.%N)
# (a scanner line: SEED X Z ice N of M pure P floor F points N quarters Q ice_at X Z pure_at X Z coarse A B C edge EP EI)
# (FORMULA_BY=size, the default: the NP best by ln(score) - 0.04 x percent outside. With the formula run on every listed
#  candidate of 651 seeds, 48 runs a seed by it hold 95% of the clusters the exact test wants now (pure 500+, nearly pure
#  600+) and all of the largest (pure 600+, nearly pure 700+); 32 runs 90% and 99%.
#  FORMULA_BY=index: 3.1 ln(score) - 0.54 x percent outside + 0.25 if it is the pure cluster, fitted for pure clusters of
#  350+: of the same it held 72% and 82%. FORMULA_BY=score: the score alone.)
awk -v least=${SP_MIN:-220} -v near=${NEAR_PCT:-8} -v by=${FORMULA_BY:-size} '{ score = $9; x = $20; z = $21; out = 0; pure = 1; if ($5 > score && ($7 - $5) * 100 <= near * $7) { score = $5; x = $17; z = $18; out = ($7 - $5) * 100 / $7; pure = 0 }   # the pure cluster, or the ice cluster with at most NEAR_PCT percent outside
    if (score >= least) print (by == "score" ? score : by == "index" ? 3.1 * log(score) - 0.54 * out + 0.25 * pure : log(score) - 0.04 * out), $1, x, -30, z, "| P", $9, $5, $7, $27 }' "$W/pairs.txt" | sort -k1,1gr | awk '!seen[$3 " " $5]++' | head -n ${NP:-48} | cut -d' ' -f2- > "$W/places.txt"
sort -k5,5nr "$W/pairs.txt" | head -n ${NI:-2} | awk -v least=${SI_MIN:-800} '$5 >= least {print $1, $17, -30, $18, "| I", $9, $5, $7, $28}' >> "$W/places.txt"
"$D/iceformula" 1 288 32 < "$W/places.txt" > "$W/formula.txt"
t2=$(date +%s.%N)
# (a formula line: SEED X Y Z | ice N of M at X Y Z pure P at X Y Z open N edge EP EI | | KIND SP SI SO SE)
awk -v least=${PURE_MIN:-500} '$15 >= least {print $15, $1, $17, $18, $19, "| S", $27, $28, $29, $30, $31, $7, $9, $15, $23}' "$W/formula.txt" | sort -k1,1nr | awk '!seen[$3 " " $4 " " $5]++' | head -n ${MP:-4} | cut -d' ' -f2- > "$W/starts.txt"
# The same test for the MN largest ice clusters of the formula that are nearly pure: NEAR_MIN samples or more with at
# most NEAR_OUT percent of them outside the ice caves. (NEAR_BIG_OUT, a wider share from NEAR_BIG samples, was tried at
# 5%: 900 tests on 2,750 seeds with 2 to 5% outside gave 8 caverns, the largest 2.8M: the same as NEAR_OUT now.) The formula's rule is the
# cautious one, and the larger a cluster the likelier that a few samples are flagged: the cavern of 5,132,158 blocks had
# 11 of 792 outside and is ice caves throughout (it was only ever measured because its 781 samples passed the old
# ICE_MIN of 780). On 651 gate seeds, of 476 such tests (600+ samples, up to 2% outside) 24 were ice caves throughout:
# 13% with 1-2 samples outside, 9% with 3-4, 2-3% beyond. In production (4,660 seeds, a narrower first version) it
# took 0.36 tests a seed and found 3 of the 4 caverns of 5M+ and 11 of the 21 of 4.5M+. Tag N.
# (And from WIDE points of the scanner's own cluster, whose window is 1,040 blocks against the formula's 576, 450 samples
#  are enough: the cavern of 5,791,476 blocks had 851 points and 573 samples.)
awk -v least=${NEAR_MIN:-600} -v pct=${NEAR_OUT:-2} -v big=${NEAR_BIG:-700} -v bigpct=${NEAR_BIG_OUT:-2} -v pure=${PURE_MIN:-500} -v wide=${WIDE:-750} '{ s = ($29 > $28 ? $29 : $28) } $15 < pure && (($7 >= least && (($9 - $7) * 100 <= pct * $9 || ($7 >= big && ($9 - $7) * 100 <= bigpct * $9))) || (s >= wide && $7 >= 450 && ($9 - $7) * 100 <= pct * $9)) {print $7, $1, $11, $12, $13, "| N", $27, $28, $29, $30, $31, $7, $9, $15, $24}' "$W/formula.txt" | sort -k1,1nr | awk '!seen[$3 " " $4 " " $5]++' | head -n ${MN:-5} | cut -d' ' -f2- >> "$W/starts.txt"
ICE_STRICT=1 "$D/icemeasure" 1 < "$W/starts.txt" > "$W/measured.txt"       # (stops as soon as a cavern leaves the ice caves)
t3=$(date +%s.%N)
awk -v least=${ICE_MIN:-1000} '$7 >= least {print $7, $1, $11, $12, $13, "| F", $27, $28, $29, $30, $31, $7, $9, $15, $23}' "$W/formula.txt" | sort -k1,1nr | awk '!seen[$3 " " $4 " " $5]++' | head -n ${MI:-1} | cut -d' ' -f2- > "$W/starts.txt"
"$D/icemeasure" 1 < "$W/starts.txt" >> "$W/measured.txt"
t4=$(date +%s.%N)
cat "$W/measured.txt" >> "$O/results.txt"; [ "${KEEP_FORMULA:-1}" = 1 ] && cat "$W/formula.txt" >> "$O/formula.txt"
best=$(awk '{for (i = 1; i <= NF; i++) if ($i == "ice" && $(i - 2) == "seconds") {if ($(i + 1) == $4 && $4 > b) b = $4; if ($(i + 1) > m) m = $(i + 1); break}} END {print b + 0, m + 0}' "$W/measured.txt")
echo "$S pairs $(echo "$t1 - $t0" | bc) formula $(echo "$t2 - $t1" | bc) measure $(echo "$t4 - $t2" | bc) scanner_pure $(sort -k9,9nr "$W/pairs.txt" | head -1 | awk '{v = $9} END {print v + 0}') scanner_ice $(sort -k5,5nr "$W/pairs.txt" | head -1 | awk '{v = $5} END {print v + 0}') formula_pure $(sort -k15,15nr "$W/formula.txt" | head -1 | awk '{v = $15} END {print v + 0}') formula_ice $(sort -k7,7nr "$W/formula.txt" | head -1 | awk '{v = $7} END {print v + 0}') fully_ice_and_most_ice $best strict $(echo "$t3 - $t2" | bc) lowest $(grep -o 'lowest [-0-9.]*' "$W/pairs.log" | cut -d' ' -f2 | awk '{v = $1} END {print v + 0}') under_ice $(grep -o '[0-9]* under ice caves' "$W/pairs.log" | cut -d' ' -f1 | awk '{v = $1} END {print v + 0}') candidates $(wc -l < "$W/formula.txt") strict_tests $(grep -c '| S ' "$W/measured.txt") full $(grep -c '| F ' "$W/measured.txt") look_best $(grep -o 'first look best [0-9]*' "$W/pairs.log" | awk '{v = $4} END {print v + 0}') look_over40 $(grep -o 'over40 [0-9]*' "$W/pairs.log" | awk '{v = $2} END {print v + 0}')" >> "$O/seeds.log"
for f in pairs.txt pairs.log places.txt formula.txt starts.txt measured.txt; do rm -f "$W/$f"; done; rmdir "$W"
