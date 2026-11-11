#!/usr/bin/env bash
# Collects AMD uProf profiles of smoke_prof (docs/plan/EXECUTION_PLAN.md, P1.4; docs/plan/PROFILE.md, section 3).
#
#   perf/uprof/collect.sh <label> [config] [seconds]
#
#   label    name of the result folder: perf/uprof/<date>_<label>/
#   config   uProf collect config (default: tbp). See: AMDuProfCLI info --list collect-configs
#            Event-based configs (assess_ext, data_access, branch, inst_access) need the hardware counters,
#            which uProf refuses while Hyper-V / WSL2 is enabled ("Only timer-based profiling (TBP) is available").
#   seconds  measured time per workload (default: 20)
#
# For every grid (64, 128) and stage (all, jacobi, advect, forces) it writes into <folder>/g<grid>_<stage>/:
#   workload.txt  what smoke_prof printed (iterations, ms per iteration)
#   report.csv    function table                (AMDuProfCLI report --cutoff 0)
#   detail.csv    samples per instruction       (AMDuProfCLI report --detail --disasm-only)
# The raw session files stay next to them but are not committed (.gitignore).
#
# Run from the repository root, in an MSYS2 / Git Bash shell, with the profile build already made:
#   cmake -S Solver -B Solver/build-prof -G "MinGW Makefiles" -DSMOKE_PROFILE_BUILD=ON
#   cmake --build Solver/build-prof --parallel
set -u

LABEL="${1:?usage: perf/uprof/collect.sh <label> [config] [seconds]}"
CONFIG="${2:-tbp}"
SECONDS_PER_RUN="${3:-20}"
UPROF="${UPROF:-/c/Program Files/AMD/AMDuProf/bin/AMDuProfCLI.exe}"
PROF="Solver/build-prof/smoke_prof.exe"
BENCH="Solver/build-prof/smoke_bench.exe"
OUT="perf/uprof/$(date +%Y-%m-%d)_${LABEL}"

[ -x "$UPROF" ] || { echo "AMDuProfCLI not found: $UPROF" >&2; exit 1; }
[ -x "$PROF" ] || { echo "build the profile build first: $PROF" >&2; exit 1; }
mkdir -p "$OUT"

# Session description for the HTML report: hardware (CPUID), RAM modules, uProf version, commit.
{
    "$BENCH" --hwinfo
    echo "RAM modules (Win32_PhysicalMemory)"
    powershell -NoProfile -Command "Get-CimInstance Win32_PhysicalMemory | ForEach-Object { '  ' + \$_.BankLabel + ': ' + [math]::Round(\$_.Capacity/1GB) + ' GiB, ' + \$_.ConfiguredClockSpeed + ' MT/s, ' + \$_.PartNumber.Trim() }" 2>/dev/null
    echo "Profiler"
    echo "  $("$UPROF" --version 2>&1 | tr -d '\r')"
    echo "  collect_config: $CONFIG"
    echo "  seconds_per_workload: $SECONDS_PER_RUN"
    echo "  date: $(date '+%Y-%m-%d %H:%M')"
} > "$OUT/session_info.txt" 2>&1

for GRID in 64 128; do
    # smoke_prof first develops the flow for 100 whole steps; sampling starts after that (--start-delay).
    # The delays are longer than the develop phase took in a trial run (64^3: under 1 s, 128^3: about 10 s).
    if [ "$GRID" -ge 128 ]; then DELAY=14; else DELAY=2; fi
    for STAGE in all jacobi advect forces; do
        SESSION="$OUT/g${GRID}_${STAGE}"
        rm -rf "$SESSION"
        echo "== $SESSION ($CONFIG, ${SECONDS_PER_RUN}s, start delay ${DELAY}s)"
        RUN_SECONDS=$((SECONDS_PER_RUN + DELAY))
        "$UPROF" collect --config "$CONFIG" --start-delay "$DELAY" -o "$(cygpath -w "$SESSION")" \
            "$(cygpath -w "$PWD/$PROF")" --grid "$GRID" --seconds "$RUN_SECONDS" --stage "$STAGE" \
            > "$OUT/collect_g${GRID}_${STAGE}.log" 2>&1
        if [ ! -d "$SESSION" ]; then
            echo "   collection FAILED, see $OUT/collect_g${GRID}_${STAGE}.log" >&2
            tail -3 "$OUT/collect_g${GRID}_${STAGE}.log" >&2
            continue
        fi
        grep -A4 '^smoke_prof:' "$OUT/collect_g${GRID}_${STAGE}.log" > "$SESSION/workload.txt"
        mv "$OUT/collect_g${GRID}_${STAGE}.log" "$SESSION/collect.log"
        WSESSION="$(cygpath -w "$SESSION")"
        "$UPROF" report -i "$WSESSION" --cutoff 0 --report-output "$WSESSION\\report.csv" > "$SESSION/report.log" 2>&1
        "$UPROF" report -i "$WSESSION" --detail --disasm-only --disasm-style att --show-percentage --cutoff 0 \
            --report-output "$WSESSION\\detail.csv" >> "$SESSION/report.log" 2>&1
        ls "$SESSION"/report.csv "$SESSION"/detail.csv > /dev/null 2>&1 || echo "   report FAILED, see $SESSION/report.log" >&2
    done
done
echo "done: $OUT"
