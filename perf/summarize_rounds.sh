#!/usr/bin/env bash
# Summarize the output of perf/run_rounds.sh (docs/plan/OPTIMIZATION.md, sections 4 and 5).
#
# Usage:  perf/summarize_rounds.sh OUTDIR BASE_LABEL [CANDIDATE_LABEL ...]
#
# Per label, grid and stage:
#   median_ms   median of the per-round medians (each round median is over --steps samples)
#   min_ms      smallest single step over all rounds
#   max_ms      largest single step over all rounds
#   rmed_lo/hi  smallest / largest per-round median
# For candidates, compared with BASE_LABEL:
#   S = median_base / median_cand,  delta% = (median_base - median_cand) / median_base
#   noise_ms    spread of the BASE per-round medians (rmed_hi - rmed_lo)        (decision D10)
#   rounds      "faster" if the candidate is faster in EVERY round, "slower" if slower in every
#               round, otherwise "mixed" (round r of the candidate is compared with round r of the base)
#   verdict     "faster"/"slower" when |median_base - median_cand| > noise_ms AND rounds agrees,
#               otherwise "noise"
set -euo pipefail
if [ "$#" -lt 2 ]; then sed -n '4p' "$0"; exit 2; fi
outdir=$1; shift
base=$1

awk -F, -v base="$base" -v labels="$*" '
FNR == 1 {
    file = FILENAME; sub(/^.*\//, "", file)
    round = file; sub(/^.*_r/, "", round); sub(/\.csv$/, "", round)
    sub(/_r[0-9]+\.csv$/, "", file)
    next
}
{
    key = file SUBSEP $1 SUBSEP $2
    n[key]++; meds[key, n[key]] = $3; byRound[key, round + 0] = $3
    if ((round + 0) > maxRound) maxRound = round + 0
    if (!(key in mn) || $4 < mn[key]) mn[key] = $4
    if (!(key in mx) || $5 > mx[key]) mx[key] = $5
    if (!(($1 SUBSEP $2) in seen)) { seen[$1 SUBSEP $2] = 1; order[++rows] = $1 SUBSEP $2 }
}
function median(key,   i, j, t, a, c) {
    c = n[key]
    for (i = 1; i <= c; ++i) a[i] = meds[key, i] + 0
    for (i = 1; i <= c; ++i) for (j = i + 1; j <= c; ++j) if (a[j] < a[i]) { t = a[i]; a[i] = a[j]; a[j] = t }
    lo[key] = a[1]; hi[key] = a[c]
    return (c % 2) ? a[(c + 1) / 2] : 0.5 * (a[c / 2] + a[c / 2 + 1])
}
function roundsAgree(bk, k,   r, faster, slower) {
    faster = 0; slower = 0
    for (r = 1; r <= maxRound; ++r) {
        if (!((bk, r) in byRound) || !((k, r) in byRound)) continue
        if (byRound[k, r] + 0 < byRound[bk, r] + 0) ++faster; else if (byRound[k, r] + 0 > byRound[bk, r] + 0) ++slower
    }
    if (faster > 0 && slower == 0) return "faster"
    if (slower > 0 && faster == 0) return "slower"
    return "mixed"
}
END {
    split(labels, L, " ")
    print "label,grid,stage,rounds,median_ms,min_ms,max_ms,rmed_lo,rmed_hi,S,delta_pct,noise_ms,rounds_agree,verdict"
    for (r = 1; r <= rows; ++r) {
        split(order[r], gs, SUBSEP)
        bk = base SUBSEP gs[1] SUBSEP gs[2]
        if (!(bk in n)) continue
        bm = median(bk); noise = hi[bk] - lo[bk]
        for (l = 1; l in L; ++l) {
            k = L[l] SUBSEP gs[1] SUBSEP gs[2]
            if (!(k in n)) continue
            m = median(k)
            S = bm / m; d = 100 * (bm - m) / bm
            diff = bm - m; if (diff < 0) diff = -diff
            agree = (L[l] == base) ? "-" : roundsAgree(bk, k)
            verdict = "-"
            if (L[l] != base) verdict = (diff > noise && agree != "mixed") ? agree : "noise"
            printf "%s,%s,%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.2f,%.6f,%s,%s\n", L[l], gs[1], gs[2], n[k], m, mn[k], mx[k], lo[k], hi[k], S, d, noise, agree, verdict
        }
    }
}' "$outdir"/*.csv
