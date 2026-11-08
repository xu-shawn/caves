#!/bin/bash
# icecaves/fleet/ptest.sh (on the box): pairtest.sh for a list of settings, one line each into ptest.txt.
# usage: fleet/ptest.sh SEEDS_FILE JOBS "NAME VAR=value ..." ["NAME VAR=value ..." ...]
cd "$(dirname "$0")/.." || exit 1; F=$1; J=$2; shift 2
for spec in "$@"; do set -- $spec; n=$1; shift; t0=$(date +%s); ./pairtest.sh $n $F $J "$@" >> ptest.txt 2>&1; echo "   ($n: $(( $(date +%s) - t0 )) s of wall time with $J jobs)" >> ptest.txt; done
touch ptest.done
