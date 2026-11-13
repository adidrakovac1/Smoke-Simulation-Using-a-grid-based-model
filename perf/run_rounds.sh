#!/usr/bin/env bash
# Interleaved measurement of several smoke_bench builds (docs/plan/OPTIMIZATION.md, section 4).
#
# Usage:
#   perf/run_rounds.sh OUTDIR ROUNDS label1=path/to/smoke_bench label2=path/to/smoke_bench ... [-- bench args]
#
# A variant may carry its own extra arguments after the path, separated by commas (no spaces), e.g.
#   sphere=Solver/build/smoke_bench.exe,--obstacle,sphere
# so one program can be measured in several modes.
#
# Every round runs every variant once; the order is rotated each round, so warm-up, background load and
# thermal effects hit all variants equally. Each run writes OUTDIR/<label>_r<round>.csv (smoke_bench CSV:
# grid,stage,median_ms,min_ms,max_ms,steps). Default bench args: --steps 100 --warmup 10 (all grids).
# Summarize with:  perf/summarize_rounds.sh OUTDIR BASE_LABEL [CANDIDATE_LABEL ...]
set -euo pipefail

if [ "$#" -lt 3 ]; then
    sed -n '4,5p' "$0"
    exit 2
fi
outdir=$1; rounds=$2; shift 2

labels=(); exes=(); extras=()
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do
    spec=${1#*=}
    labels+=("${1%%=*}"); exes+=("${spec%%,*}")
    if [ "$spec" = "${spec#*,}" ]; then extras+=(""); else extras+=("${spec#*,}"); fi
    shift
done
[ "${1:-}" = "--" ] && shift
benchArgs=("$@")
[ "${#benchArgs[@]}" -eq 0 ] && benchArgs=(--steps 100 --warmup 10)

mkdir -p "$outdir"
n=${#labels[@]}
for ((r = 1; r <= rounds; ++r)); do
    for ((v = 0; v < n; ++v)); do
        idx=$(( (v + r - 1) % n ))
        echo "round $r/$rounds: ${labels[$idx]}"
        extra=()
        if [ -n "${extras[$idx]}" ]; then IFS=, read -r -a extra <<< "${extras[$idx]}"; fi
        "${exes[$idx]}" "${benchArgs[@]}" ${extra[@]+"${extra[@]}"} --output "$outdir/${labels[$idx]}_r$r.csv"
    done
done
