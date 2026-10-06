#!/bin/bash
# The exact measurer against its reference: 13 caverns of 40M to 54.6M blocks (tests/caverns13.txt) must come out with
# the same volume, core, bounding box and /tp point as tests/caverns13.expected, which the game's own code
# confirmed (measure.sh). Run it after any change to native/gamegen.hpp or native/cavemeasure.cpp, with the CPU's
# densities (as here) and, on a box, with the card's (GPU_DENSITY=1 tests/check_measure.sh).
# usage: tests/check_measure.sh [BINARY=native/cavemeasure] [THREADS=6]      exit status 0: identical
D="$(cd "$(dirname "$0")/.." && pwd)"; BIN=${1:-$D/native/cavemeasure}; T=${2:-6}; out=$(mktemp)
"$BIN" "$T" < "$D/tests/caverns13.txt" | awk '{for (i = NF - 4; i <= NF; i++) $i = ""; print}' | sort > "$out"   # (without the last five fields: the chunk count, which depends on how far ahead chunks are fetched, and the seconds)
if diff -q "$out" "$D/tests/caverns13.expected" > /dev/null; then echo "13 caverns identical ($(md5sum < "$out" | cut -c1-32))"; rm -f "$out"; exit 0; fi
echo "DIFFERENT from the reference:"; diff "$out" "$D/tests/caverns13.expected" | head -20; rm -f "$out"; exit 1
