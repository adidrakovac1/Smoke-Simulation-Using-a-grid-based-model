#!/usr/bin/env python3
"""Turns AMD uProf CSV reports into one self-contained HTML page (docs/plan/EXECUTION_PLAN.md, P1.5).

Input: a folder made by perf/uprof/collect.sh. Each sub-folder (g64_jacobi, g128_all, ...) is one session with
    report.csv    function table      (AMDuProfCLI report --cutoff 0)
    detail.csv    samples per instruction (AMDuProfCLI report --detail --disasm-only --show-percentage)
    workload.txt  what smoke_prof printed
and the folder itself has session_info.txt (hardware, profiler version).

Output: one HTML file with all CSS, JavaScript and data inside (no network access needed). Sessions that
profiled the same work with different uProf configs are joined into one "workload"; the page has one tab per
view, modelled on Intel VTune: summary, hotspots, microarchitecture, cache and memory, instructions, compare.

    python tools/uprof_report.py perf/uprof/2026-11-11_tbp [more folders ...] -o perf/uprof/report.html
    python tools/uprof_report.py perf/uprof/2026-11-11_tbp --summary      (text summary, no HTML)

The parser is written against the uProf 6.0 CSV layout seen on PC-2 with the "tbp", "assess_ext" and
"inst_access" configs. Metric columns are read by name, so time-based and event-based reports go through the
same code; event-based reports add a DERIVED METRICS section (formulas) and per-instruction counter columns.
Only the Python standard library is used.
"""

import argparse
import csv
import html
import json
import re
import sys
from pathlib import Path

# Which step phase a function belongs to (names from Solver/src/stages). First match wins.
PHASES = [
    # Obstacle loops over the sparse lists (Solver/src/stages/Obstacles.cpp). Listed first: "enforceSolidVelocity"
    # and friends must not fall into another phase by a partial name match.
    ("Prepreke", r"extendIntoSolids|setInSolids|reflectVelocityIntoSolids|enforceSolidVelocity|clearSolidScalars"
                 r"|applyPorousDrag|applyPorousFilter"),
    ("Jacobi", r"jacobi"),
    ("Projekcija (ostalo)", r"computeDivergence|subtractPressureGradient|divergenceRow|gradientRow|\bproject\b"),
    # sampleTrilinear / clampToBox: helpers of the M3 advection (Solver/demo/M3Solver.inc), if not inlined.
    ("Advekcija", r"advect|sampleTrilinear|clampToBox"),
    # [Vv] / [Cc]: the M3 step calls it applyVorticityConfinement, the optimized solver vorticityRow / confinementRow.
    ("Sile", r"[Vv]orticity|[Cc]onfinement|Buoyancy|applyForces"),
    ("Rubni uslovi", r"setBoundary|setVelocityBoundary"),
    ("Izvor", r"addSources"),
    # ucrtbase.dll / vcruntime: the C runtime; in smoke_prof its samples are memcpy / memset (no symbols for it).
    ("Kopiranje i punjenje", r"memcpy|memset|memmove|::fill\b|Field3D::clear|SmokeState::clear|ucrtbase\.dll|vcruntime"),
]
OTHER_PHASE = "Ostalo"

# Instruction classes, used to answer "what kind of work gets the samples" (hypotheses H2 and H3).
GATHER_MNEMONICS = {"vpextrd", "pextrd", "vpextrq", "pextrq", "vinsertps", "insertps", "vextracti128",
                    "vextractf128", "vinserti128", "vinsertf128", "vmovd", "movd", "vpinsrd", "pinsrd",
                    "vunpcklps", "unpcklps", "vmovlhps", "movlhps", "vmovq", "movq"}


def short_name(full):
    """'smoke::serial::(anonymous namespace)::advectRow3(float*, ...)' -> 'advectRow3'."""
    name = full.replace("(anonymous namespace)::", "")
    name = name.split("(", 1)[0].strip()
    return name.rsplit("::", 1)[-1] if "::" in name and not name.endswith("::") else name


def phase_of(full):
    for phase, pattern in PHASES:
        if re.search(pattern, full):
            return phase
    return OTHER_PHASE


def classify(instruction):
    """Rough class of one AT&T-syntax instruction (destination is the last operand)."""
    parts = instruction.strip().split(None, 1)
    if not parts:
        return "ostalo"
    mnemonic = parts[0].lower()
    # uProf prints some mnemonics with an AT&T size suffix (vinsertpsl, movssl): drop it for the lookups.
    if mnemonic.endswith("l") and (mnemonic[:-1] in GATHER_MNEMONICS or mnemonic[:-1] in ("vmovss", "movss")):
        mnemonic = mnemonic[:-1]
    operands = parts[1] if len(parts) > 1 else ""
    # Split on commas that are not inside "(base,index,scale)".
    pieces = re.split(r",\s*(?![^()]*\))", operands) if operands else []
    last = pieces[-1] if pieces else ""
    reads_memory = any("(" in piece for piece in pieces[:-1])
    writes_memory = "(" in last and len(pieces) > 1
    if mnemonic.startswith("j") or mnemonic in ("call", "ret", "retq", "callq"):
        return "grananje"
    if mnemonic.startswith("nop") or mnemonic == "xchg":
        return "poravnanje (nop)"
    if "div" in mnemonic:
        return "dijeljenje"
    if "sqrt" in mnemonic:
        return "korijen"
    if mnemonic in GATHER_MNEMONICS:
        return "gather (slaganje vektora)"
    if mnemonic in ("vmovss", "movss") and reads_memory:
        return "pojedinacno citanje (movss)"
    if writes_memory and mnemonic.lstrip("v").startswith("mov"):
        return "upis u memoriju"
    if writes_memory:
        return "racun s upisom u memoriju"
    if reads_memory:
        return "racun s citanjem iz memorije" if not mnemonic.lstrip("v").startswith("mov") else "citanje iz memorije"
    if mnemonic.startswith(("lea", "cmp", "test", "inc", "dec")) or mnemonic in ("add", "sub", "mov", "xor", "addl", "movl"):
        return "brojaci petlje i adrese"
    return "racun u registrima"


# How many floats one instruction works on, judged by its registers and mnemonic. Used for the "data
# parallelism" view: which part of the time goes to 8-wide (AVX), up to 4-wide (SSE) and one-at-a-time work.
WIDTH_NAMES = ["256-bitni registri (ymm): 8 brojeva odjednom", "128-bitni registri (xmm), vektorski: do 4 broja odjednom",
               "skalarno: jedan broj po instrukciji", "bez float registara: brojači petlje, adrese, skokovi"]


def width_index(instruction):
    parts = instruction.strip().split(None, 1)
    if len(parts) < 2:
        return 3
    mnemonic, operands = parts[0].lower(), parts[1]
    if "%ymm" in operands:
        return 0
    if "%xmm" in operands:
        base = mnemonic[1:] if mnemonic.startswith("v") else mnemonic
        # addss, mulsd, movss, cvtsi2ssl, cvttss2si, comiss, movd / movq: one value in the low part of the register.
        if re.search(r"(ss|sd)[lq]?$|ss2|sd2|comis|^mov[dq]$", base):
            return 2
        return 1
    return 3


def number(text):
    text = text.strip().rstrip("%").replace(",", "")
    try:
        return float(text)
    except ValueError:
        return None


def read_rows(path):
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        return [row for row in csv.reader(handle)]


def parse_report(path):
    """Header fields, monitored events and the function table of report.csv."""
    rows = read_rows(path)
    info, events, functions, metrics, derived = {}, [], [], [], {}
    index = 0
    while index < len(rows):
        row = rows[index]
        if len(row) >= 2 and row[0].endswith(":") and row[1] and not row[0].endswith("Events:"):
            info[row[0].rstrip(":")] = row[1]
        elif row and row[0].endswith("Events:"):
            # "TBP Events:,Name,Interval,Description" or
            # "PMC Events:,Name,Interval,Unitmask,Countmask,Invert Countmask,User,OS,Description": find columns by name.
            columns = [cell.strip() for cell in row]
            interval_at = columns.index("Interval") if "Interval" in columns else 2
            description_at = columns.index("Description") if "Description" in columns else 3
            index += 1
            while index < len(rows) and rows[index] and rows[index][0] == "":
                cells = rows[index]
                events.append({"name": cells[1], "interval": cells[interval_at] if len(cells) > interval_at else "",
                               "description": " ".join(cells[description_at].split()) if len(cells) > description_at else ""})
                index += 1
            continue
        elif row and row[0].strip() == "DERIVED METRICS":
            # Event-based configs only: "Metric Name,Expression,Description", one row per derived column.
            index += 2
            while index < len(rows) and rows[index]:
                cells = rows[index]
                if len(cells) >= 2:
                    derived[re.sub(r"\s+", " ", cells[0]).strip()] = " ".join(cells[1].split())
                index += 1
            continue
        elif row and "HOTTEST FUNCTIONS" in row[0] and not functions:
            header = rows[index + 1]
            # Columns: FUNCTION, one or more metrics, Module.
            metrics = [re.sub(r"\s+", " ", name).strip() for name in header[1:-1]]
            index += 2
            while index < len(rows) and rows[index]:
                cells = rows[index]
                values = [number(cell) for cell in cells[1:1 + len(metrics)]]
                functions.append({"full": cells[0], "name": short_name(cells[0]), "phase": phase_of(cells[0]),
                                  "module": Path(cells[-1].replace("\\", "/")).name, "values": values})
                index += 1
            continue
        index += 1
    return info, events, metrics, functions, derived


def parse_detail(path):
    """Per-instruction sample shares from detail.csv: {full function name: [instruction, ...]}.

    uProf lists only the instructions that got at least one sample, so addresses have gaps. The code bytes give
    the instruction length, which lets the page show where such a gap is.
    """
    result, extra = {}, []
    if not path.exists():
        return result, extra
    rows = read_rows(path)
    current = None
    in_detail = False
    for row in rows:
        if row and row[0] == "Function" and len(row) >= 5 and row[1] == "Address":
            in_detail = True
            current = None
            # Event-based configs: more metric columns follow the first one (same header for every function).
            extra = [re.sub(r"\s+", " ", name).strip() for name in row[5:]]
            continue
        if not in_detail:
            continue
        if not row:
            current = None
            continue
        if row[0]:
            full = re.sub(r"\s*\(Showing only disassembly\)\s*$", "", row[0])
            current = result.setdefault(full, [])
        elif current is not None and len(row) >= 5:
            share = number(row[4])
            if share is not None:
                current.append({"address": row[1], "text": row[2], "share": share, "cls": classify(row[2]),
                                "length": len(row[3].split()),
                                "values": [number(cell) for cell in row[5:5 + len(extra)]]})
    return result, extra


def parse_workload(path):
    workload = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("smoke_prof"):
                obstacle = re.search(r"prepreka=(\w+)", line)
                if obstacle:
                    workload["prepreka"] = obstacle.group(1)
                version = re.search(r"verzija=(\w+)", line)
                if version:
                    workload["verzija"] = version.group(1)
            elif ":" in line:
                key, value = line.split(":", 1)
                workload[key.strip()] = value.strip()
    return workload


def kind_of(metrics):
    """Which kind of uProf config made the session, judged by its columns."""
    names = " ".join(metrics)
    if "CPU_TIME" in names:
        return "time"      # tbp: time-based sampling
    if "%L1_DC_MISSES" in names:
        return "pmc"       # assess_ext: core counters (cycles, branches, data cache)
    if "IC_MISS_RATIO" in names:
        return "fe"        # inst_access: frontend counters (instruction cache, op cache, ITLB)
    return "other"


# Instruction class names are stored once; every instruction carries only an index into this list.
CLASS_NAMES = []


def class_index(name):
    if name not in CLASS_NAMES:
        CLASS_NAMES.append(name)
    return CLASS_NAMES.index(name)


def load_session(folder, group):
    info, events, metrics, functions, derived = parse_report(folder / "report.csv")
    detail, detail_metrics = parse_detail(folder / "detail.csv")
    match = re.match(r"g(\d+)_(\w+)$", folder.name)
    grid, stage = (int(match.group(1)), match.group(2)) if match else (0, folder.name)
    total = sum(f["values"][0] or 0.0 for f in functions) if metrics else 0.0
    for function in functions:
        function["share"] = 100.0 * (function["values"][0] or 0.0) / total if total else 0.0
        instructions = sorted(detail.get(function["full"], []), key=lambda item: int(item["address"], 16))
        classes = {}
        for item in instructions:
            classes[item["cls"]] = classes.get(item["cls"], 0.0) + item["share"]
        function["classes"] = sorted(classes.items(), key=lambda pair: -pair[1])
        # vw: share of the session's samples (%) per register width class (WIDTH_NAMES), summed over the function.
        widths = [0.0, 0.0, 0.0, 0.0]
        for item in instructions:
            widths[width_index(item["text"])] += item["share"]
        function["vw"] = [round(value, 4) for value in widths]
        # Short keys: there are about 50 000 instruction rows in the page. a = address, t = text, s = share (%),
        # c = class index, n = length in bytes, v = the other counter columns.
        function["ins"] = [{"a": item["address"], "t": item["text"], "s": item["share"], "c": class_index(item["cls"]),
                            "n": item["length"], "v": item["values"]} for item in instructions]
        del function["full"]
    workload = parse_workload(folder / "workload.txt")
    return {"id": f"{group}/{folder.name}", "group": group, "kind": kind_of(metrics), "grid": grid, "stage": stage,
            "obstacle": workload.get("prepreka", "none"),
            # Sessions made before smoke_prof had --version ran the optimized solver, which is now called m4.
            "version": workload.get("verzija", "m4"),
            "label": f"{grid}^3 {stage} ({group})", "info": info, "events": events, "metrics": metrics,
            "derived": derived, "detailMetrics": detail_metrics,
            "total": total, "functions": functions, "workload": workload}


def load_all(folders):
    sessions, infos = [], []
    for folder in folders:
        folder = Path(folder)
        info_file = folder / "session_info.txt"
        infos.append({"group": folder.name,
                      "text": info_file.read_text(encoding="utf-8", errors="replace") if info_file.exists() else ""})
        for sub in sorted(folder.iterdir(), key=lambda p: (len(p.name), p.name)):
            if (sub / "report.csv").exists():
                sessions.append(load_session(sub, folder.name))
    return sessions, infos


STAGE_ORDER = ["all", "jacobi", "advect", "forces"]
STAGE_NAMES = {"all": "cijeli korak", "jacobi": "Jacobi", "advect": "advekcija", "forces": "sile"}
OBSTACLE_NAMES = {"none": "bez prepreke", "sphere": "kugla", "blanket": "deka"}
# smoke_prof --version. m3 and m3auto are the M3 step (commit 73ceb9e) from Solver/demo, m4 is the current solver.
VERSION_NAMES = {"m3": "naivna (M3)", "m3auto": "M3 + auto-vektorizacija", "m4": "finalna (M4)"}


def build_workloads(sessions):
    """Joins the sessions that profiled the same work with different uProf configs.

    perf/uprof/collect.sh names its folders <date>_<label>, and the labels used so far end in tbp, ebp or inst.
    Sessions whose folder names differ only in that ending, with the same grid and stage, are one "workload":
    the page then shows time from the tbp session next to counters from the ebp session.
    """
    workloads, by_key = [], {}
    for index, session in enumerate(sessions):
        run = re.sub(r"_(tbp|ebp|inst)$", "", session["group"])
        key = (run, session["grid"], session["stage"])
        workload = by_key.get(key)
        if workload is not None and session["kind"] in workload["s"]:
            # Two sessions of the same kind for one key: keep them apart under the full folder name.
            run = session["group"]
            key = (run, session["grid"], session["stage"])
            workload = by_key.get(key)
        if workload is None:
            stage = STAGE_NAMES.get(session["stage"], session["stage"])
            obstacle = OBSTACLE_NAMES.get(session["obstacle"], session["obstacle"])
            version = VERSION_NAMES.get(session["version"], session["version"])
            # Runs named <date>_ver_<version> were collected together to compare the versions: same "vset".
            vset = re.match(r"(.*)_ver_\w+$", run)
            workload = {"run": run, "grid": session["grid"], "stage": session["stage"],
                        "obstacle": session["obstacle"], "version": session["version"],
                        "vset": vset.group(1) if vset else None,
                        "label": f"{session['grid']}³ · {stage} · {obstacle} · {version} · {run}", "s": {}}
            by_key[key] = workload
            workloads.append(workload)
        workload["s"][session["kind"]] = index
    order = {name: position for position, name in enumerate(STAGE_ORDER)}
    workloads.sort(key=lambda w: (w["run"], order.get(w["stage"], len(order)), w["grid"]))
    return workloads


def load_official(path):
    """Speedups of the final version measured by the protocol D10 on the normal Release build (perf/results.csv).

    The rows "M4_total_vs_B0" / "M4_total_vs_B1" compare the final M4 solver with the M3 code built without
    vectorization (B0, here version m3) and with the default flags (B1, here m3auto). A later row for the same
    grid and stage replaces an earlier one (the file is written in time order).
    """
    official = {}
    if not path or not Path(path).exists():
        return official
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        for row in csv.DictReader(handle):
            base = {"M4_total_vs_B0": "m3", "M4_total_vs_B1": "m3auto"}.get(row.get("technique"))
            speedup = number(row.get("speedup_S") or "")
            if not base or speedup is None or not (row["grid_nx"] == row["grid_ny"] == row["grid_nz"]):
                continue
            official.setdefault(base, {}).setdefault(row["grid_nx"], {})[row["stage"]] = {
                "S": speedup, "id": row["entry_id"], "date": row["date"], "machine": row["machine"]}
    return official


def phase_shares(session):
    shares = {}
    for function in session["functions"]:
        shares[function["phase"]] = shares.get(function["phase"], 0.0) + function["share"]
    return shares


def print_summary(sessions):
    for session in sessions:
        workload = session["workload"]
        print(f"\n=== {session['label']}: {session['total']:.3f} {session['metrics'][0] if session['metrics'] else ''}"
              f" | ms_po_ponavljanju={workload.get('ms_po_ponavljanju', '?')}")
        print("  faze: " + ", ".join(f"{phase} {share:.1f}%" for phase, share in
                                      sorted(phase_shares(session).items(), key=lambda pair: -pair[1]) if share >= 0.05))
        for function in session["functions"][:5]:
            if function["share"] < 1.0:
                continue
            print(f"  {function['share']:5.1f}%  {function['name']}  [{function['phase']}]")
            if len(session["metrics"]) > 1:
                print("          " + ", ".join(f"{name}={value:g}" for name, value in
                                               zip(session["metrics"], function["values"]) if value is not None))
            print("          klase: " + ", ".join(f"{name} {value:.1f}" for name, value in function["classes"][:6]))
            for item in sorted(function["ins"], key=lambda item: -item["s"])[:6]:
                print(f"          {item['s']:6.2f}%  {item['t']}")


PAGE = r"""<!doctype html>
<html lang="bs">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Profil simulacije dima (AMD uProf)</title>
<style>
:root { --bg:#f6f7f9; --card:#ffffff; --text:#1b1f24; --muted:#5b6672; --line:#d9dee5; --accent:#2f6fb7;
        --bar:#4c8fd6; --bar2:#e0913a; --good:#2e8b57; --bad:#c2453c; --code:#eef1f5; --heat:214,69,56; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#14171b; --card:#1d2127; --text:#e6e9ed; --muted:#9aa5b1; --line:#333a44; --accent:#7fb2ea;
          --bar:#4c8fd6; --bar2:#e0913a; --good:#5fc08a; --bad:#ef7d74; --code:#262b33; --heat:232,92,78; }
}
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--text); font:15px/1.5 system-ui, "Segoe UI", sans-serif; }
header.top { position:sticky; top:0; z-index:5; background:var(--card); border-bottom:1px solid var(--line); }
.topin { max-width:1280px; margin:0 auto; padding:10px 16px 0; display:flex; gap:10px 18px; flex-wrap:wrap; align-items:center; }
.title { font-size:18px; font-weight:650; }
nav { max-width:1280px; margin:0 auto; padding:6px 16px 0; display:flex; gap:2px; overflow-x:auto; }
nav button { font:inherit; border:0; background:none; color:var(--muted); padding:7px 12px; cursor:pointer;
             border-bottom:3px solid transparent; white-space:nowrap; }
nav button:hover { color:var(--text); }
nav button.on { color:var(--accent); border-bottom-color:var(--accent); font-weight:600; }
main { max-width:1280px; margin:0 auto; padding:6px 16px 60px; }
h2 { font-size:18px; margin:0 0 8px; } h3 { font-size:15px; margin:16px 0 6px; }
p { margin:6px 0; } .muted { color:var(--muted); } .small { font-size:13px; }
section { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:14px 18px; margin:14px 0; }
table { border-collapse:collapse; width:100%; font-size:13.5px; }
th, td { padding:5px 8px; border-bottom:1px solid var(--line); text-align:left; vertical-align:middle; }
th { white-space:nowrap; position:sticky; top:0; background:var(--card); z-index:1; font-weight:600; }
th.sortable { cursor:pointer; user-select:none; }
th.sorted::after { content:" \25BC"; font-size:10px; } th.sorted.asc::after { content:" \25B2"; }
th.wrap { white-space:normal; min-width:72px; }
td.num, th.num { text-align:right; font-variant-numeric:tabular-nums; }
.scroll { overflow:auto; max-height:560px; }
pre, code { font-family:Consolas, "Cascadia Mono", monospace; font-size:12.5px; }
pre { background:var(--code); padding:10px 12px; border-radius:8px; overflow-x:auto; margin:6px 0; white-space:pre-wrap; }
select, input { font:inherit; padding:5px 8px; border:1px solid var(--line); border-radius:6px; background:var(--card); color:var(--text); max-width:100%; }
input[type=number] { width:70px; }
.row { display:flex; gap:8px 14px; flex-wrap:wrap; align-items:center; margin:8px 0; }
.bar { height:13px; background:var(--bar); border-radius:3px; min-width:1px; }
.barcell { min-width:110px; width:16%; }
.stack { display:flex; height:22px; border-radius:5px; overflow:hidden; border:1px solid var(--line); min-width:60px; }
.stack div { height:100%; } .stack.thin { height:13px; border:0; border-radius:3px; min-width:2px; }
.legend { display:flex; flex-wrap:wrap; gap:4px 14px; font-size:13px; margin-top:6px; }
.dot { display:inline-block; width:10px; height:10px; border-radius:2px; margin-right:5px; }
.pos { color:var(--bad); } .neg { color:var(--good); }
.tag { display:inline-block; padding:1px 7px; border-radius:10px; font-size:12px; border:1px solid var(--line); white-space:nowrap; }
.chip { display:inline-block; padding:1px 8px; border-radius:10px; font-size:12px; border:1px solid var(--line); margin-right:4px; white-space:nowrap; }
.chip.off { opacity:.5; text-decoration:line-through; }
.note { border-left:3px solid var(--accent); padding:4px 12px; margin:10px 0; }
.warn { border-left:3px solid var(--bar2); padding:4px 12px; margin:10px 0; }
details summary { cursor:pointer; color:var(--accent); }
tr.pick { cursor:pointer; } tr.pick:hover td { box-shadow:inset 0 0 0 999px rgba(127,127,127,.10); }
tr.active td { font-weight:600; box-shadow:inset 0 0 0 999px rgba(127,127,127,.14); }
tr.head td { font-weight:650; background:var(--code); }
tr.gap td { color:var(--muted); font-size:12px; padding:1px 8px; text-align:center; }
.tiles { display:grid; grid-template-columns:repeat(auto-fill, minmax(190px, 1fr)); gap:10px; }
.tile { border:1px solid var(--line); border-radius:8px; padding:9px 12px; }
.tile .k { font-size:12.5px; color:var(--muted); } .tile .v { font-size:22px; font-weight:650; font-variant-numeric:tabular-nums; }
.tile .s { font-size:12px; color:var(--muted); line-height:1.35; }
.ice { display:flex; width:100%; gap:2px; overflow:hidden; }
.ice > div { min-width:0; overflow:hidden; }
.ice .ph { height:24px; line-height:24px; padding:0 6px; color:#fff; font-size:12.5px; font-weight:600; overflow:hidden; white-space:nowrap; text-overflow:ellipsis; border-radius:3px 3px 0 0; }
.ice .fns { display:flex; gap:1px; margin-top:1px; }
.ice .fn { height:30px; line-height:30px; padding:0 4px; font-size:12px; overflow:hidden; white-space:nowrap; text-overflow:clip; color:#fff; cursor:pointer; min-width:0; opacity:.82; }
.ice .fn:hover { opacity:1; }
#err { display:none; background:#c2453c; color:#fff; padding:8px 16px; white-space:pre-wrap; font-family:Consolas, monospace; }
.tab { display:none; } .tab.on { display:block; }
.seg { display:inline-flex; border:1px solid var(--line); border-radius:8px; overflow:hidden; }
.seg button { font:inherit; border:0; background:none; color:var(--text); padding:6px 14px; cursor:pointer; border-right:1px solid var(--line); }
.seg button:last-child { border-right:0; }
.seg button.on { background:var(--accent); color:#fff; font-weight:600; }
.cards { display:grid; grid-template-columns:repeat(auto-fill, minmax(330px, 1fr)); gap:14px; }
.card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:14px 18px; }
.card h3 { margin:0 0 2px; font-size:16px; }
.card .q { color:var(--muted); font-size:13px; margin:0 0 8px; }
.card .big { font-size:26px; font-weight:650; line-height:1.2; font-variant-numeric:tabular-nums; }
.card .unit { font-size:14px; font-weight:400; color:var(--muted); }
.card .plain { background:var(--code); border-radius:8px; padding:8px 12px; margin-top:10px; font-size:13.5px; }
.verdict { display:inline-block; margin:8px 0 2px; padding:2px 10px; border-radius:12px; font-size:13px; font-weight:600; border:1px solid var(--line); }
.verdict.good { color:var(--good); border-color:var(--good); } .verdict.bad { color:var(--bad); border-color:var(--bad); }
.boxes { display:flex; gap:4px; flex-wrap:wrap; margin:8px 0; }
.boxes span { width:22px; height:22px; border-radius:4px; border:1px solid var(--line); }
.boxes span.on { background:var(--accent); border-color:var(--accent); }
.lead { font-size:17px; line-height:1.45; margin:10px 0; }
.two { display:grid; grid-template-columns:repeat(auto-fit, minmax(420px, 1fr)); gap:14px; }
.duo { min-width:130px; } .duo div { height:7px; border-radius:2px; min-width:1px; margin:2px 0; }
ul { margin:6px 0; padding-left:22px; }
</style>
</head>
<body>
<header class="top">
  <div class="topin">
    <div class="title">Profil simulacije dima</div>
    <label>Mjerenje <select id="wsel"></select></label>
    <span id="kinds"></span>
  </div>
  <nav id="tabs"></nav>
</header>
<div id="err"></div>
<main>
<p class="muted small">AMD uProf, program <code>smoke_prof</code>, serijska verzija (jedna nit). Generisano: __GENERATED__.
Fajl je samostalan: nema vanjskih skripti ni stilova. Šta koji prikaz znači i šta se iz ovih podataka <b>ne može</b> dobiti: kartica "O mjerenju".</p>

<div class="tab" id="t-grade">
  <section>
    <h2>Ocjena verzije</h2>
    <p class="small muted">Izaberi verziju programa i veličinu mreže. Sve ispod je izračunato iz snimljenog profila te verzije i,
    gdje piše, upoređeno s naivnom verzijom na istoj mreži. Izbor ovdje mijenja i "Mjerenje" u zaglavlju, pa ostale kartice
    pokazuju istu verziju.</p>
    <div class="row">
      <span>Verzija</span><span class="seg" id="grVer"></span>
      <span>Mreža</span><span class="seg" id="grGrid"></span>
    </div>
    <div id="grHead"></div>
  </section>
  <div class="cards" id="grCards"></div>
  <section>
    <h2>Kako je ocjena dobijena</h2>
    <ul class="small">
      <li><b>Nema školskih ocjena ni pragova.</b> Ne znamo izmjerenu granicu između "dobro" i "loše" za ovaj računar, pa je svaka
      ocjena ili izmjeren broj ili odnos prema naivnoj verziji ("2.9 puta brže"). Zelena oznaka znači bolje od naivne, crvena lošije.</li>
      <li><b>Brzina</b> je iz programa <code>smoke_prof</code> u profilnoj gradnji pod profilerom. Služi za odnos među verzijama snimljenim
      isti dan. Službeno ubrzanje (obična gradnja, protokol D10) je u <code>docs/plan/PERF_LOG.md</code>, odjeljak 7, i navedeno je gdje postoji.</li>
      <li><b>Instrukcije po koraku</b> nisu izbrojane direktno, pa je odnos dvije verzije procijenjen na dva načina: (1) iz vremena koraka
      i CPI-ja (vrijeme = instrukcije × ciklusa po instrukciji ÷ takt, uz pretpostavku istog takta u obje sesije) i (2) iz broja uzoraka
      brojača instrukcija po koraku (uz pretpostavku da su obje sesije uzorkovane jednako dugo). Ta dva načina se razlikuju do 30 %,
      a uzrok nije utvrđen, pa je prikazan raspon. Poređenja "po koraku" (promašaji, grananja) koriste način (1).</li>
      <li><b>Promašaji po koraku</b>: uProf daje promašaje na 1000 instrukcija. Verzije izvršavaju različit broj instrukcija, pa se za
      poređenje ta stopa množi brojem instrukcija po koraku.</li>
      <li><b>Širina instrukcija</b> (8, do 4 ili 1 broj odjednom) je određena po registrima u ispisu instrukcije (<code>ymm</code>,
      <code>xmm</code>) i po imenu instrukcije, a udio po uzorcima vremena. Uzorak se ponekad pripiše susjednoj instrukciji ("skid"),
      pa su udjeli približni.</li>
      <li><b>Niti</b>: program je građen bez OpenMP-a i radi na jednoj niti (odluka D8). Broj niti računara je iz CPUID-a.</li>
    </ul>
  </section>
</div>

<div class="tab" id="t-summary">
  <section>
    <h2>Sažetak mjerenja</h2>
    <div class="tiles" id="sumTiles"></div>
    <p class="small muted" id="sumNote"></p>
  </section>
  <section>
    <h2>Najvrelije funkcije</h2>
    <p class="small muted">Klik na red otvara instrukcije te funkcije.</p>
    <div class="scroll"><table id="sumHot"></table></div>
  </section>
  <section>
    <h2>Udio faza koraka</h2>
    <div id="sumPhases"></div>
    <div class="legend" id="sumLegend"></div>
  </section>
  <section>
    <h2>Kako je skupljeno</h2>
    <div class="scroll"><table id="sumCollect"></table></div>
  </section>
</div>

<div class="tab" id="t-hot">
  <section>
    <h2>Hotspots: gdje ide vrijeme</h2>
    <div class="row">
      <label>Osnova <select id="hotBasis"></select></label>
      <label>Grupiši <select id="hotGroup">
        <option value="func">samo funkcije</option><option value="phase">faza → funkcija</option><option value="module">modul → funkcija</option></select></label>
      <label>Filter imena <input id="hotFilter" placeholder="npr. advect" size="14"></label>
      <span class="muted small" id="hotNote"></span>
    </div>
    <h3>Mapa: faza → funkcija</h3>
    <p class="small muted">Širina je udio u sesiji. Gornji red su faze koraka, donji funkcije te faze. Ovo <b>nije</b> stablo poziva
    (ko je koga pozvao nije skupljano); funkcije su fazama pridružene po imenu.</p>
    <div id="hotIce"></div>
    <h3>Tabela (klik na zaglavlje sortira, klik na red otvara instrukcije)</h3>
    <div class="scroll"><table id="hotTable"></table></div>
  </section>
</div>

<div class="tab" id="t-uarch">
  <section>
    <h2>Mikroarhitektura: zašto je mjesto sporo</h2>
    <div id="uarchBody">
      <p class="small">Vrijeme kaže <i>gdje</i>; brojači u procesoru pomažu reći <i>zašto</i>. Najvažnija kolona je <b>CPI</b>
      (ciklusa po instrukciji): ista funkcija s istim instrukcijama i većim CPI-jem znači da procesor na nešto čeka.
      Ostale kolone traže na šta: pogrešna grananja, promašaji keša, čitanja preko granice linije keša.</p>
      <div class="row">
        <label>Prikaži funkcije s najmanje <input type="number" id="uarchMin" min="0" step="0.5"> % ciklusa</label>
        <span class="muted small">Omjeri na malo uzoraka su nepouzdani, zato su male funkcije sakrivene.</span>
      </div>
      <div class="scroll"><table id="uarchTable"></table></div>
      <p class="small muted">Boja ćelije je relativna: najveća vrijednost u koloni među prikazanim funkcijama je najtamnija. Nema
      zadanih pragova "dobro / loše"; njih nismo mjerili.</p>
      <p class="warn small"><b>Čega ovdje nema.</b> VTune na Intelu dijeli cikluse na četiri grupe (Front-End Bound, Bad Speculation,
      Back-End Bound, Retiring). Za to trebaju brojači izdatih i izvršenih mikro operacija i zastoja pri otpremanju, kojih u
      konfiguracijama <code>assess_ext</code> i <code>inst_access</code> nema. Tu podjelu zato ne prikazujemo niti je procjenjujemo.</p>
    </div>
  </section>
</div>

<div class="tab" id="t-cache">
  <section>
    <h2>Keš i memorija</h2>
    <div id="cacheBody">
      <div id="cacheSizes"></div>
      <div class="tiles" id="cacheTiles" style="margin-top:10px"></div>
      <h3>Po funkcijama</h3>
      <div class="row">
        <label>Prikaži funkcije s najmanje <input type="number" id="cacheMin" min="0" step="0.5"> % ciklusa</label>
        <span class="legend" id="cacheLegend" style="margin:0"></span>
      </div>
      <div class="scroll"><table id="cacheTable"></table></div>
      <div class="note small">
        <p><b>Kako čitati.</b> <b>L1 promašaji %</b>: koliki dio pristupa memoriji nije nađen u L1 (uključuje i zahtjeve prefetchera).
        <b>Punjenje L1</b> ("demand refill"): instrukcija je tražila podatak kojeg nema u L1, pa je linija keša od 64 B donesena iz
        L2, iz L3 ili iz RAM-a. Što dalji izvor, to duže čekanje. Broje se samo punjenja na koja je instrukcija <i>čekala</i>; linije koje
        je prefetcher donio unaprijed se ovdje ne vide. "/1000" znači na 1000 izvršenih instrukcija.</p>
        <p><b>Udio svih punjenja</b> kaže koja funkcija je uzrok: broj punjenja funkcije (stopa puta njene instrukcije) podijeljen
        zbirom za sve funkcije sesije.</p>
      </div>
      <p class="warn small"><b>Granice.</b> Brojači L3 keša i memorijskog kontrolera (<code>L3 PMC</code>, <code>UMC PMC</code>) na ovom
      računaru nisu dostupni, pa L3 promašaje vidimo samo posredno, kao punjenja L1 iz RAM-a. Nema ni IBS-a, pa ne znamo
      <i>koju adresu</i> (koje polje) je promašaj tražio; VTune-ov prikaz po objektima memorije se ne može napraviti.</p>
    </div>
  </section>
</div>

<div class="tab" id="t-asm">
  <section>
    <h2>Instrukcije funkcije</h2>
    <div class="row">
      <label>Funkcija <select id="asmFn"></select></label>
      <label>Sesija <select id="asmKind"></select></label>
      <label>Redoslijed <select id="asmOrder"><option value="addr">po adresi (kao u kodu)</option><option value="share">najvrelije prvo</option></select></label>
      <label>Boji po <select id="asmHeat"></select></label>
    </div>
    <p class="small" id="asmTitle"></p>
    <div id="asmClasses"></div>
    <div class="scroll" style="max-height:640px"><table id="asmTable"></table></div>
    <p class="note small"><b>Oprez ("skid").</b> Uzorak se često pripiše instrukciji <i>poslije</i> one koja je stvarno spora (procesor
    prekid obradi tek kad spora instrukcija završi). Visok postotak na jednoj instrukciji znači "ovdje ili neposredno prije".
    uProf ispisuje samo instrukcije koje su dobile bar jedan uzorak; red "…" označava preskočene instrukcije bez uzoraka.
    Kolone brojača na instrukcijama s malo uzoraka su nepouzdane (mali brojevi u omjeru).</p>
  </section>
</div>

<div class="tab" id="t-cmp">
  <section>
    <h2>Poređenje verzija</h2>
    <p class="small muted">Isti posao (cijeli korak simulacije, ista mreža, isti scenarij, bit po bit isti rezultat), dvije verzije koda.
    A je polazna verzija, B ona s kojom se poredi. Obje su snimljene isti dan, na istom računaru, istim postupkom.</p>
    <div id="cmpBody">
      <div class="row">
        <span>Mreža</span><span class="seg" id="cmpGrid"></span>
        <label>A <select id="cmpA"></select></label>
        <label>B <select id="cmpB"></select></label>
      </div>
      <div class="tiles" id="cmpTiles"></div>
      <div id="cmpWhy"></div>
      <h3>Po fazama koraka</h3>
      <p class="small muted">Vrijeme faze = udio uzoraka te faze × vrijeme jednog koraka. "S = A ÷ B" je odnos vremena, a
      "Instrukcije A ÷ B" odnos broja instrukcija za isti posao. <b>Iznad 1</b>: B je brža, odnosno izvrši manje instrukcija (zeleno).
      <b>Ispod 1</b>: B je sporija, odnosno izvrši više instrukcija (crveno).</p>
      <div class="scroll"><table id="cmpPhases"></table></div>
      <h3>Koliko brojeva jedna instrukcija obradi odjednom</h3>
      <p class="small muted">Udio vremena po širini registara koje instrukcije koriste. Ovo je paralelizam unutar jedne niti (SIMD).</p>
      <div id="cmpWidth"></div>
      <h3>Brojači procesora</h3>
      <div class="scroll"><table id="cmpCounters"></table></div>
      <h3>Najvrelije funkcije svake verzije</h3>
      <p class="small muted">Verzije imaju različite funkcije (finalna je jezgra podijelila na redove i AVX2 fajlove), pa se ne spajaju po imenu.
      Klik na red otvara instrukcije te funkcije.</p>
      <div class="two">
        <div><b id="cmpNameA"></b><div class="scroll"><table id="cmpFnA"></table></div></div>
        <div><b id="cmpNameB"></b><div class="scroll"><table id="cmpFnB"></table></div></div>
      </div>
      <div id="cmpOfficial"></div>
      <p class="warn small"><b>Kako čitati.</b> Vremena su iz profilne gradnje pod profilerom i vrijede kao odnos A prema B, ne kao
      apsolutni rezultat. Računar nije bio posebno umiren. Službena ubrzanja se mjere na običnoj Release gradnji po protokolu D10
      (<code>docs/plan/PERF_LOG.md</code>).</p>
    </div>
  </section>
</div>

<div class="tab" id="t-all">
  <section>
    <h2>Sve sesije: udio faza</h2>
    <p class="small muted">Svaka traka je 100% uzoraka te sesije, podijeljeno po fazama koraka.</p>
    <div class="scroll" style="max-height:none"><table id="allStacks"></table></div>
    <div class="legend" id="allLegend"></div>
  </section>
  <section>
    <h2>Popis sesija</h2>
    <div class="scroll"><table id="allTable"></table></div>
  </section>
</div>

<div class="tab" id="t-about">
  <section>
    <h2>Kako čitati ovaj izvještaj</h2>
    <p><b>Šta je mjereno.</b> Profiler koji uzorkuje prekida program u pravilnim razmacima i zapiše koja se instrukcija
    upravo izvršavala. Funkcija koja dobije više uzoraka troši više vremena. Kod <b>vremenskog uzorkovanja (TBP)</b> razmak je 1 ms.
    Kod <b>hardverskih brojača</b> uzorak se uzme svaki put kad brojač u procesoru odbroji zadani broj događaja (ciklusa,
    instrukcija, promašaja keša), pa udio uzoraka kaže gdje se ti događaji dešavaju.</p>
    <p><b>Mjerenje</b> u padajućoj listi gore je jedan posao (mreža, faza, prepreka, dan). Isti posao je sniman više puta, svaki put
    s drugom konfiguracijom uProf-a: vrijeme (<code>tbp</code>), brojači jezgra (<code>assess_ext</code>) i brojači frontenda
    (<code>inst_access</code>). Kartice uzimaju podatke iz one sesije koja ih ima.</p>
    <p class="small muted">Profilna gradnja ima <code>-g -fno-omit-frame-pointer</code>; služi za nalaženje mjesta. Pod profilerom je
    program sporiji, naročito s brojačima. Ubrzanja (S, Δ) se mjere na običnoj Release gradnji (docs/plan/PERF_LOG.md).</p>
    <h3>Prikazi po uzoru na Intel VTune i šta od njih imamo</h3>
    <div class="scroll"><table>
      <tr><th>VTune prikaz</th><th>Ovdje</th><th>Napomena</th></tr>
      <tr><td>Summary</td><td>Sažetak</td><td>CPU vrijeme, CPI, najvrelije funkcije, udio faza.</td></tr>
      <tr><td>Hotspots, Bottom-up</td><td>Hotspots</td><td>Tabela funkcija s trakama, grupisanje po fazi ili modulu.</td></tr>
      <tr><td>Top-down Tree, Caller/Callee, Flame Graph</td><td>samo mapa faza → funkcija</td><td>Stablo poziva nije skupljano (<code>Call Stack Sampling: False</code>). Za pravi flame graph treba novo skupljanje s opcijom za stek poziva.</td></tr>
      <tr><td>Microarchitecture Exploration</td><td>Mikroarhitektura</td><td>CPI, grananja, neporavnata čitanja, frontend. Podjela na Front-End / Bad Speculation / Back-End / Retiring se iz ovih brojača ne može izračunati.</td></tr>
      <tr><td>Memory Access</td><td>Keš i memorija</td><td>L1 promašaji i punjenja L1 iz L2 / L3 / RAM-a po funkciji. Nema L3 brojača ni adresa podataka (IBS), pa nema prikaza po objektima.</td></tr>
      <tr><td>Assembly</td><td>Instrukcije</td><td>Instrukcije po adresi s toplotnom skalom, i brojači po instrukciji.</td></tr>
      <tr><td>Source</td><td>nema</td><td>Izvještaji uProf-a su pravljeni s <code>--disasm-only</code>, bez linija izvornog koda.</td></tr>
      <tr><td>Compare</td><td>Poređenje verzija</td><td>Naivna (M3) i finalna (M4) verzija na istoj mreži: vrijeme po fazi, ubrzanje, instrukcije, CPI, keš, širina instrukcija.</td></tr>
      <tr><td>(nema u VTune-u)</td><td>Ocjena</td><td>Jedna verzija opisana jednostavnim jezikom: brzina, niti, SIMD, keš, grananja, gdje ide vrijeme.</td></tr>
      <tr><td>Timeline, Threads</td><td>nema</td><td>Program ima jednu nit; vremenska osa uzoraka nije izvezena iz uProf-a.</td></tr>
    </table></div>
    <h3>Kolone</h3>
    <div id="metricHelp"></div>
  </section>
  <section>
    <h2>Računar, gradnja i profiler</h2>
    <div id="hardware"></div>
  </section>
</div>
</main>

<script id="data" type="application/json">__DATA__</script>
<script>
"use strict";
window.onerror = function (message, source, line) {
  const box = document.getElementById("err");
  box.style.display = "block";
  box.textContent += "JS greška: " + message + " (red " + line + ")\n";
};
const DATA = JSON.parse(document.getElementById("data").textContent);
const S = DATA.sessions, WL = DATA.workloads, CLASSES = DATA.classes;
const PHASE_COLORS = {"Jacobi":"#4c8fd6","Projekcija (ostalo)":"#7da7d9","Advekcija":"#e0913a","Sile":"#5aa469",
  "Rubni uslovi":"#b07cc6","Prepreke":"#2f9e9e","Izvor":"#b89a1f","Kopiranje i punjenje":"#8a94a0","Ostalo":"#c2453c"};
const PHASES = Object.keys(PHASE_COLORS);
const KIND_NAME = {time:"vrijeme (tbp)", pmc:"brojači (assess_ext)", fe:"frontend (inst_access)", other:"ostalo"};
const KIND_BASIS = {time:"vrijeme (uzorci svake 1 ms)", pmc:"ciklusi (brojač, assess_ext)", fe:"ciklusi (brojač, inst_access)"};
const TABS = [["grade","Ocjena"],["cmp","Poređenje verzija"],["summary","Sažetak"],["hot","Hotspots"],["uarch","Mikroarhitektura"],
  ["cache","Keš i memorija"],["asm","Instrukcije"],["all","Sve sesije"],["about","O mjerenju"]];
// smoke_prof --version: m3 = the M3 step without vectorization (naive), m3auto = the same code with the default
// compiler flags, m4 = the current optimized solver.
const VERSIONS = ["m3", "m3auto", "m4"];
const VSHORT = {m3:"Naivna", m3auto:"Auto-vektorizirana", m4:"Finalna"};
const VLONG = {m3:"Naivna verzija (M3 kod, bez vektorizacije)", m3auto:"M3 kod + automatska vektorizacija kompajlera",
  m4:"Finalna, optimizirana verzija (M4)"};
const WIDTH_COLORS = ["#2e8b57", "#4c8fd6", "#e0913a", "#8a94a0"];
const WIDTH_SHORT = ["8 brojeva odjednom (AVX)", "do 4 broja odjednom (SSE)", "jedan po jedan (skalarno)", "ostalo (brojači, adrese, skokovi)"];
const OFFICIAL_STAGE = {whole_step:"cijeli korak", sources:"izvori", forces:"sile", advect_velocity:"advekcija brzine", projection:"projekcija",
  advect_scalars:"advekcija skalara", jacobi_iteration:"jedna Jacobi iteracija"};
const UNIT = {all:"jedno ponavljanje = jedan korak simulacije", jacobi:"jedno ponavljanje = jedna Jacobi iteracija",
  advect:"jedno ponavljanje = pet advektiranih polja", forces:"jedno ponavljanje = jedan poziv applyForces"};
const REFILLS = [["L1_DEMAND_DC_REFILLS_LOCAL_L2 (PTI)","iz L2","#4c8fd6"],["L1_DEMAND_DC_REFILLS_LOCAL_CACHE (PTI)","iz L3","#e0913a"],
  ["L1_DEMAND_DC_REFILLS_EXTERNAL_CACHE_LOCAL (PTI)","iz keša druge grupe jezgara","#b07cc6"],["L1_DEMAND_DC_REFILLS_LOCAL_DRAM (PTI)","iz RAM-a","#c2453c"]];
const METRIC_HELP = {
  "CPU_TIME": "Vrijeme procesora u sekundama: broj uzoraka puta razmak uzorkovanja (1 ms). Funkcija sa 2.0 s je bila zatečena u izvršavanju 2000 puta.",
  "PTI": "\"PTI\" = na 1000 izvršenih instrukcija (per thousand instructions). Npr. 12 promašaja PTI = 12 promašaja na svakih 1000 instrukcija."
};
// Columns of the event-based configs, by exact name. "Demand" fills are the ones a load or store itself had to
// wait for; lines brought in ahead of time by the hardware prefetcher are not counted there.
const METRIC_EXACT = {
  "CPI": "Ciklusa po instrukciji. Manje je bolje. Ista funkcija s većim CPI-jem čeka na nešto (memoriju, dijeljenje, pogrešno grananje).",
  "CYCLES_NOT_IN_HALT": "Ciklusi procesora dok nit radi, kao broj uzoraka tog događaja. U sesijama s brojačima se \"udio %\" računa iz ove kolone, pa znači udio ciklusa.",
  "RETIRED_INST": "Izvršene (\"retired\") instrukcije, kao broj uzoraka tog događaja.",
  "RETIRED_BR_INST (PTI)": "Grananja na 1000 izvršenih instrukcija.",
  "%RETIRED_BR_INST_MISP": "Postotak grananja koja je procesor pogrešno predvidio. Svako takvo grananje baca već započeti rad.",
  "L1_DC_ACCESSES (PTI)": "Čitanja i upisa u memoriju na 1000 instrukcija.",
  "%L1_DC_MISSES": "Postotak pristupa memoriji koji nisu nađeni u L1 kešu, pa su tražili L2. Uključuje i zahtjeve prefetchera.",
  "L1_DEMAND_DC_REFILLS_LOCAL_L2 (PTI)": "Punjenja L1 iz L2 na 1000 instrukcija: podatak je bio u L2 istog jezgra.",
  "L1_DEMAND_DC_REFILLS_LOCAL_CACHE (PTI)": "Punjenja L1 iz L3 (ili iz L2 drugog jezgra) na 1000 instrukcija.",
  "L1_DEMAND_DC_REFILLS_LOCAL_DRAM (PTI)": "Punjenja L1 iz RAM-a na 1000 instrukcija: podatak nije bio ni u jednom kešu. Broji samo punjenja na koja je instrukcija čekala (\"demand\"), ne i linije koje je prefetcher donio unaprijed.",
  "L1_DEMAND_DC_REFILLS_EXTERNAL_CACHE_LOCAL (PTI)": "Punjenja L1 iz keša druge grupe jezgara (CCX) na 1000 instrukcija.",
  "MISALIGNED_LOADS (PTI)": "Čitanja koja prelaze granicu linije keša (64 B) ili stranice, na 1000 instrukcija.",
  "STLI_OTHER (PTI)": "Čitanja koja su morala čekati raniji upis na adresu koja se s njima djelimično preklapa, na 1000 instrukcija.",
  "WCB_WRITE (PTI)": "Upisi kroz \"write combining\" bafer na 1000 instrukcija (koristi se kod non-temporal upisa).",
  "%WCB_NOT_FULLLINE64B_TO_CLOSE": "Postotak zatvaranja write combining bafera prije nego što je upisana cijela linija od 64 B.",
  "%WCB_CLOSE_TO_WRITE": "Odnos zatvaranja i upisa write combining bafera, u postocima.",
  "SSE_AVX_STALLS (PTC)": "Zastoji zbog miješanja SSE i AVX koda, na 1000 ciklusa (PTC = per thousand cycles).",
  "INEFFECTIVE_SW_PF (PTI)": "Softverski prefetch koji nije ništa donio (podatak je već bio u kešu ili na putu), na 1000 instrukcija.",
  // inst_access: the frontend (fetching and decoding instructions).
  "OP_CACHE_64B_FETCH_MISS_RATIO": "Udio pristupa kešu dekodiranih mikro operacija (op cache) koji su promašili (0.02 = 2%). Promašaj znači da procesor instrukcije mora ponovo dekodirati.",
  "IC_MISS_RATIO": "Udio pristupa L1 kešu instrukcija koji su promašili. Vruća petlja uglavnom ide iz op cachea, pa je pristupa ovom kešu malo.",
  "IC_MISSES (PTI)": "Promašaji L1 keša instrukcija na 1000 instrukcija.",
  "L2_CACHE_ACCESSES_FROM_IC_MISSES (PTI)": "Zahtjevi prema L2 zbog promašaja keša instrukcija, na 1000 instrukcija.",
  "L2_CACHE_MISSES_FROM_IC_MISSES (PTI)": "Po opisu uProf-a: promašaji L2 zbog promašaja keša instrukcija. U našim sesijama se ne slaže s kolonom IC_MISSES (docs/plan/PROFILE.md, 4.7), pa je ne koristimo.",
  "L1_ITLB_REQUESTS (PTI)": "Zahtjevi prema ITLB-u (kešu prevođenja adresa stranica s kodom), na 1000 instrukcija.",
  "L1_ITLB_MISSES (PTI)": "Promašaji prvog nivoa ITLB-a na 1000 instrukcija.",
  "L2_ITLB_MISSES (PTI)": "Promašaji drugog nivoa ITLB-a (slijedi šetnja po tabeli stranica), na 1000 instrukcija.",
  "L1_ITLB_MISS_RATE": "Promašaji prvog nivoa ITLB-a po izvršenoj instrukciji.",
  "L2_ITLB_MISS_RATE": "Promašaji drugog nivoa ITLB-a po izvršenoj instrukciji."
};

const el = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
const fmt = (v, d = 2) => (v === null || v === undefined || Number.isNaN(v)) ? "–" : Number(v).toFixed(d);
const heat = (x) => x < 0.02 ? "transparent" : `rgba(var(--heat),${(0.05 + 0.6 * Math.min(1, x)).toFixed(3)})`;
const tag = (phase) => phase ? `<span class="tag"><span class="dot" style="background:${PHASE_COLORS[phase] || "#999"}"></span>${esc(phase)}</span>` : "";
const helpOf = (name) => METRIC_EXACT[name] || (name.startsWith("CPU_TIME") ? METRIC_HELP.CPU_TIME : "");

// ---------- state (kept in the URL hash, so a view can be linked) ----------
const state = {w: 0, tab: "grade", fn: null, basis: "time", group: "func", asmKind: "time", asmOrder: "addr",
               heat: "share", min: 1, cg: 64, ca: "m3", cb: "m4"};
function readHash() {
  for (const part of location.hash.replace(/^#/, "").split("&")) {
    const [key, value] = part.split("=");
    if (!(key in state) || value === undefined) continue;
    const text = decodeURIComponent(value);
    state[key] = typeof state[key] === "number" ? (+text || 0) : text;
  }
  state.w = Math.max(0, Math.min(WL.length - 1, state.w));
  if (!TABS.some(([id]) => id === state.tab)) state.tab = "grade";
}
function writeHash() {
  const keys = ["w", "tab"].concat(state.fn ? ["fn"] : [], state.tab === "cmp" ? ["cg", "ca", "cb"] : []);
  history.replaceState(null, "", "#" + keys.map((k) => k + "=" + encodeURIComponent(state[k])).join("&"));
}

// ---------- data access ----------
const W = () => WL[state.w];
const ses = (w, kind) => (w && kind in w.s) ? S[w.s[kind]] : null;
const mi = (s, name) => name === "CPU_TIME" ? s.metrics.findIndex((m) => m.startsWith("CPU_TIME")) : s.metrics.indexOf(name);
const val = (s, f, name) => { const i = mi(s, name); return i < 0 ? null : f.values[i]; };
const byName = (s, name) => s ? s.functions.find((f) => f.name === name) : null;
const pickBasis = (w, wanted) => ses(w, wanted) ? wanted : ["time", "pmc", "fe"].find((k) => ses(w, k));

// One value for a group of functions. uProf prints ratios per function; to join functions the ratio has to be
// weighted by its own denominator, which the table also has (as a rate per instruction).
function agg(s, fs, name) {
  if (mi(s, name) < 0) return null;
  const get = (f, n) => val(s, f, n) || 0;
  const sum = (fn) => fs.reduce((a, f) => a + fn(f), 0);
  const inst = (f) => get(f, "RETIRED_INST"), cycles = (f) => get(f, "CYCLES_NOT_IN_HALT");
  if (name.startsWith("CPU_TIME") || name === "CYCLES_NOT_IN_HALT" || name === "RETIRED_INST") return sum((f) => get(f, name));
  if (name === "CPI") { const d = sum(inst); return d ? sum(cycles) / d : null; }
  let weight = null;
  if (name.endsWith("(PTI)") || name.endsWith("_MISS_RATE")) weight = inst;
  else if (name.endsWith("(PTC)")) weight = cycles;
  else if (name === "%RETIRED_BR_INST_MISP") weight = (f) => get(f, "RETIRED_BR_INST (PTI)") * inst(f);
  else if (name === "%L1_DC_MISSES") weight = (f) => get(f, "L1_DC_ACCESSES (PTI)") * inst(f);
  if (!weight) return null;   // ratios whose denominator is not in the table (op cache, instruction cache)
  const d = sum(weight);
  return d ? sum((f) => get(f, name) * weight(f)) / d : null;
}
// Share of all events of one "per 1000 instructions" column that belong to function f.
function eventShare(s, f, name) {
  const count = (x) => (val(s, x, name) || 0) * (val(s, x, "RETIRED_INST") || 0);
  const total = s.functions.reduce((a, x) => a + count(x), 0);
  return total ? 100 * count(f) / total : null;
}
function phaseShares(s) {
  const out = {};
  for (const f of s.functions) out[f.phase] = (out[f.phase] || 0) + f.share;
  return out;
}
function rowOf(s, f) {
  const m = {};
  s.metrics.forEach((n, i) => { m[n] = f.values[i]; });
  return {_id: f.name, name: f.name, phase: f.phase, module: f.module, share: f.share, m, f};
}
function rowAgg(s, fs, name, extra) {
  const m = {};
  for (const n of s.metrics) m[n] = agg(s, fs, n);
  return Object.assign({name, share: fs.reduce((a, f) => a + f.share, 0), m, _head: true, _noheat: true}, extra || {});
}
function stackHtml(s, thin) {
  const shares = phaseShares(s);
  let out = "";
  for (const p of PHASES) if (shares[p] > 0.05) out += `<div style="width:${shares[p]}%;background:${PHASE_COLORS[p]}" title="${esc(p)}: ${fmt(shares[p], 1)}%"></div>`;
  return `<div class="stack${thin ? " thin" : ""}">${out}</div>`;
}
const legendHtml = () => PHASES.map((p) => `<span><span class="dot" style="background:${PHASE_COLORS[p]}"></span>${esc(p)}</span>`).join("");

// ---------- generic sortable table ----------
// cols: {key, label, title, num, d (decimals), get(row) -> value used for sorting and heat, html(row), heat, wrap}
// opt: {sort (default key), groups: [{head, rows}], pinned: [rows], onPick(id), active(row)}
const sortState = {};
function drawTable(id, cols, rows, opt = {}) {
  const st = sortState[id] || (sortState[id] = {key: opt.sort || null, asc: false});
  const sortCol = cols.find((c) => c.key === st.key && c.get);
  const sorted = (list) => {
    if (!sortCol) return list;
    return list.slice().sort((a, b) => {
      const x = sortCol.get(a), y = sortCol.get(b);
      if (x == null || y == null) return (x == null) - (y == null);
      return (x < y ? -1 : x > y ? 1 : 0) * (st.asc ? 1 : -1);
    });
  };
  const groups = opt.groups || [{head: null, rows}];
  const all = groups.flatMap((g) => g.rows);
  const max = {};
  for (const c of cols) if (c.heat) max[c.key] = Math.max(0, ...all.filter((r) => !r._noheat).map((r) => +c.get(r) || 0));
  const cell = (c, r) => {
    const raw = c.get ? c.get(r) : null;
    const text = c.html ? c.html(r) : (c.num ? fmt(raw, c.d === undefined ? 2 : c.d) : esc(raw == null ? "" : raw));
    // No colour for a value that prints as zero: a column of zeros must not look hot.
    const shown = c.html ? raw : parseFloat(text);
    const style = (c.heat && !r._noheat && max[c.key] > 0 && shown > 0) ? ` style="background:${heat(raw / max[c.key])}"` : "";
    return `<td class="${c.num ? "num" : ""}"${style}>${text}</td>`;
  };
  const line = (r) => `<tr class="${r._head ? "head" : ""}${opt.onPick && r._id != null ? " pick" : ""}${opt.active && opt.active(r) ? " active" : ""}"`
    + (r._id != null ? ` data-id="${esc(r._id)}"` : "") + ">" + cols.map((c) => cell(c, r)).join("") + "</tr>";
  let out = "<thead><tr>" + cols.map((c) => `<th data-key="${esc(c.key)}" class="${c.num ? "num " : ""}${c.wrap ? "wrap " : ""}${c.get ? "sortable " : ""}`
    + `${st.key === c.key ? "sorted" + (st.asc ? " asc" : "") : ""}" title="${esc(c.title || "")}">${c.label}</th>`).join("") + "</tr></thead><tbody>";
  for (const r of opt.pinned || []) out += line(r);
  for (const g of groups) { if (g.head) out += line(g.head); out += sorted(g.rows).map(line).join(""); }
  const table = el(id);
  table.innerHTML = out + "</tbody>";
  table.querySelectorAll("th.sortable").forEach((th) => th.onclick = () => {
    const col = cols.find((c) => c.key === th.dataset.key);
    if (st.key === col.key) st.asc = !st.asc; else { st.key = col.key; st.asc = !col.num; }
    drawTable(id, cols, rows, opt);
  });
  if (opt.onPick) table.querySelectorAll("tr.pick").forEach((tr) => tr.onclick = () => opt.onPick(tr.dataset.id));
}
const metricCol = (name, label, extra) => Object.assign({key: name, label, title: helpOf(name), num: true, wrap: true,
  get: (r) => r.m[name]}, extra || {});
const barCol = (max) => ({key: "bar", label: "", html: (r) => `<div class="barcell"><div class="bar" style="width:${Math.min(100, 100 * r.share / (r._head ? 100 : max))}%;`
  + `background:${PHASE_COLORS[r.phase] || "var(--bar)"}"></div></div>`});
function openFunction(name, kind) {
  state.fn = name; state.tab = "asm";
  if (kind) state.asmKind = kind;
  render();
}
const tile = (k, v, s) => `<div class="tile"><div class="k">${k}</div><div class="v">${v}</div><div class="s">${s || ""}</div></div>`;
const missing = (what) => `<p class="warn">Za ovo mjerenje nema sesije s ${what}. Izaberi drugo mjerenje u listi gore (oznake pored liste pokazuju šta postoji).</p>`;

// ---------- Summary ----------
function renderSummary() {
  const w = W(), st = ses(w, "time"), sp = ses(w, "pmc");
  const base = st || sp || ses(w, "fe");
  let tiles = "";
  if (st) {
    tiles += tile("CPU vrijeme (iz uzoraka)", fmt(st.total, 2) + " s", "trajanje profila " + esc(st.info["Profile Duration"] || "–"));
    tiles += tile("Ponavljanja", esc(st.workload.ponavljanja || "–"), UNIT[w.stage] || "");
    tiles += tile("ms po ponavljanju", fmt(parseFloat(st.workload.ms_po_ponavljanju), 3), "profilna gradnja pod profilerom; nije mjera ubrzanja");
  } else tiles += tile("CPU vrijeme", "nije mjereno", "nema sesije s vremenskim uzorkovanjem");
  if (sp) {
    const a = (n) => agg(sp, sp.functions, n);
    tiles += tile("CPI (ciklusa po instrukciji)", fmt(a("CPI"), 2), "cijela sesija; manje je bolje");
    tiles += tile("Pogrešno predviđena grananja", fmt(a("%RETIRED_BR_INST_MISP"), 2) + " %", fmt(a("RETIRED_BR_INST (PTI)"), 0) + " grananja na 1000 instrukcija");
    tiles += tile("L1 promašaji", fmt(a("%L1_DC_MISSES"), 1) + " %", "dio pristupa memoriji koji nije nađen u L1");
    tiles += tile("Punjenja L1 iz RAM-a", fmt(a("L1_DEMAND_DC_REFILLS_LOCAL_DRAM (PTI)"), 2), "na 1000 instrukcija");
    tiles += tile("Čitanja preko linije keša", fmt(a("MISALIGNED_LOADS (PTI)"), 0), "na 1000 instrukcija");
  } else tiles += tile("Hardverski brojači", "nije mjereno", "nema sesije assess_ext za ovo mjerenje");
  el("sumTiles").innerHTML = tiles;
  el("sumNote").innerHTML = sp ? "Vrijednosti brojača za cijelu sesiju su izračunate iz tabele funkcija (omjer svake funkcije ponderisan njenim brojem instrukcija, odnosno grananja ili pristupa memoriji)." : "";

  const top = base.functions.slice().sort((a, b) => b.share - a.share).slice(0, 8).map((f) => {
    const r = rowOf(base, f), p = byName(sp, f.name);
    r.cpi = p ? val(sp, p, "CPI") : null; r.l1 = p ? val(sp, p, "%L1_DC_MISSES") : null; r.ram = p ? val(sp, p, "L1_DEMAND_DC_REFILLS_LOCAL_DRAM (PTI)") : null;
    return r;
  });
  const maxShare = top.length ? top[0].share : 1;
  const cols = [{key: "name", label: "Funkcija", get: (r) => r.name}, {key: "phase", label: "Faza", html: (r) => tag(r.phase)}, barCol(maxShare),
    {key: "share", label: st ? "udio vremena %" : "udio ciklusa %", num: true, get: (r) => r.share}];
  if (st) cols.push({key: "t", label: "CPU vrijeme (s)", num: true, d: 3, get: (r) => r.m[st.metrics[0]]});
  if (sp) cols.push({key: "cpi", label: "CPI", title: helpOf("CPI"), num: true, heat: true, get: (r) => r.cpi},
    {key: "l1", label: "L1 promašaji %", title: helpOf("%L1_DC_MISSES"), num: true, d: 1, heat: true, get: (r) => r.l1},
    {key: "ram", label: "Punjenja iz RAM-a /1000", title: helpOf("L1_DEMAND_DC_REFILLS_LOCAL_DRAM (PTI)"), num: true, heat: true, get: (r) => r.ram});
  drawTable("sumHot", cols, top, {sort: "share", onPick: (name) => openFunction(name, base.kind)});

  let phases = "<table>";
  for (const kind of ["time", "pmc", "fe"]) {
    const s = ses(w, kind);
    if (!s) continue;
    const shares = phaseShares(s);
    const text = PHASES.filter((p) => shares[p] >= 1).sort((a, b) => shares[b] - shares[a]).map((p) => `${p} ${fmt(shares[p], 1)}%`).join(", ");
    phases += `<tr><td style="white-space:nowrap">${esc(KIND_BASIS[kind])}</td><td style="width:60%">${stackHtml(s)}<div class="small muted">${esc(text)}</div></td></tr>`;
  }
  el("sumPhases").innerHTML = phases + "</table>";
  el("sumLegend").innerHTML = legendHtml();

  let rows = "<tr><th>Sesija</th><th>Vrsta</th><th>Tip profila (uProf)</th><th class='num'>Trajanje</th><th>Početak</th><th class='num'>Ponavljanja</th><th class='num'>ms po ponavljanju</th></tr>";
  for (const kind of ["time", "pmc", "fe"]) {
    const s = ses(w, kind);
    if (!s) continue;
    rows += `<tr><td>${esc(s.id)}</td><td>${esc(KIND_NAME[kind])}</td><td>${esc(s.info["Profile Session Type"] || "")}</td><td class="num">${esc(s.info["Profile Duration"] || "")}</td>`
      + `<td>${esc(s.info["Profile Start Time"] || "")}</td><td class="num">${esc(s.workload.ponavljanja || "–")}</td><td class="num">${esc(s.workload.ms_po_ponavljanju || "–")}</td></tr>`;
  }
  el("sumCollect").innerHTML = rows;
}

// ---------- Hotspots ----------
function renderHot() {
  const w = W();
  state.basis = pickBasis(w, state.basis);
  const s = ses(w, state.basis);
  el("hotBasis").innerHTML = ["time", "pmc", "fe"].filter((k) => ses(w, k)).map((k) => `<option value="${k}">${esc(KIND_BASIS[k])}</option>`).join("");
  el("hotBasis").value = state.basis; el("hotGroup").value = state.group;
  const text = el("hotFilter").value.toLowerCase();
  const list = s.functions.filter((f) => !text || f.name.toLowerCase().includes(text));
  el("hotNote").textContent = `${list.length} od ${s.functions.length} funkcija; zbir ${fmt(s.total, state.basis === "time" ? 3 : 0)} ${s.metrics[0]}`;

  // Map: phases on top, their functions below. Functions under 0.3% of the session are left out of the picture.
  const shares = phaseShares(s);
  let ice = "";
  for (const p of PHASES.filter((x) => shares[x] >= 0.3).sort((a, b) => shares[b] - shares[a])) {
    const fs = s.functions.filter((f) => f.phase === p && f.share >= 0.3).sort((a, b) => b.share - a.share);
    ice += `<div style="flex:${shares[p]} 1 0"><div class="ph" style="background:${PHASE_COLORS[p]}" title="${esc(p)}: ${fmt(shares[p], 1)}%">${esc(p)} ${fmt(shares[p], 1)}%</div><div class="fns">`
      + fs.map((f) => `<div class="fn" data-name="${esc(f.name)}" style="flex:${f.share} 1 0;background:${PHASE_COLORS[p]}" title="${esc(f.name)}: ${fmt(f.share, 2)}%">${esc(f.name)} ${fmt(f.share, 1)}%</div>`).join("")
      + "</div></div>";
  }
  el("hotIce").innerHTML = `<div class="ice">${ice}</div>`;
  el("hotIce").querySelectorAll(".fn").forEach((d) => d.onclick = () => openFunction(d.dataset.name, state.basis));

  const maxShare = Math.max(1e-9, ...list.map((f) => f.share));
  const primary = s.metrics[0];
  const cols = [{key: "name", label: state.group === "func" ? "Funkcija" : "Grupa / funkcija", get: (r) => r.name.toLowerCase(),
                 html: (r) => r._head ? esc(r.name) : (state.group === "func" ? "" : "&nbsp;&nbsp;&nbsp;") + esc(r.name)},
    barCol(maxShare), {key: "share", label: state.basis === "time" ? "udio vremena %" : "udio ciklusa %", num: true, get: (r) => r.share},
    metricCol(primary, state.basis === "time" ? "CPU vrijeme (s)" : "ciklusi (uzoraka)", {d: state.basis === "time" ? 3 : 0, wrap: false})];
  if (state.basis !== "time") cols.push(metricCol("RETIRED_INST", "instrukcije (uzoraka)", {d: 0}), metricCol("CPI", "CPI", {heat: true}));
  cols.push({key: "phase", label: "Faza", get: (r) => r.phase || "", html: (r) => tag(r.phase)}, {key: "module", label: "Modul", get: (r) => r.module || "", html: (r) => `<span class="muted">${esc(r.module || "")}</span>`});
  const opt = {sort: "share", onPick: (name) => openFunction(name, state.basis), active: (r) => r._id === state.fn};
  if (state.group !== "func") {
    const key = state.group === "phase" ? "phase" : "module", map = new Map();
    for (const f of list) { if (!map.has(f[key])) map.set(f[key], []); map.get(f[key]).push(f); }
    opt.groups = [...map.entries()].map(([name, fs]) => ({head: rowAgg(s, fs, name, key === "phase" ? {phase: name} : {module: ""}), rows: fs.map((f) => rowOf(s, f))}))
      .sort((a, b) => b.head.share - a.head.share);
  }
  drawTable("hotTable", cols, list.map((f) => rowOf(s, f)), opt);
}

// ---------- Microarchitecture ----------
function renderUarch() {
  const w = W(), s = ses(w, "pmc"), sf = ses(w, "fe");
  if (!s) { el("uarchBody").style.display = "none"; if (!el("uarchMissing")) el("uarchBody").insertAdjacentHTML("beforebegin", `<div id="uarchMissing">${missing("hardverskim brojačima (assess_ext)")}</div>`); return; }
  if (el("uarchMissing")) el("uarchMissing").remove();
  el("uarchBody").style.display = "";
  el("uarchMin").value = state.min;
  const fe = (name) => (r) => { const f = r._head ? null : byName(sf, r.name); return f ? val(sf, f, name) : null; };
  const cols = [{key: "name", label: "Funkcija", get: (r) => r.name.toLowerCase(),
     html: (r) => (r.phase ? `<span class="dot" style="background:${PHASE_COLORS[r.phase] || "#999"}" title="${esc(r.phase)}"></span>` : "") + esc(r.name)},
    {key: "share", label: "udio ciklusa %", num: true, wrap: true, get: (r) => r.share},
    metricCol("CPI", "CPI", {heat: true}),
    metricCol("RETIRED_BR_INST (PTI)", "Grananja /1000 instr.", {d: 1}),
    metricCol("%RETIRED_BR_INST_MISP", "Pogrešno predviđena %", {heat: true}),
    metricCol("%L1_DC_MISSES", "L1 promašaji %", {d: 1, heat: true}),
    metricCol("MISALIGNED_LOADS (PTI)", "Čitanja preko linije keša /1000", {d: 0, heat: true}),
    metricCol("STLI_OTHER (PTI)", "Čekanje na raniji upis /1000", {d: 1, heat: true}),
    metricCol("SSE_AVX_STALLS (PTC)", "SSE/AVX zastoji /1000 cikl.", {d: 2, heat: true})];
  if (sf) cols.push(
    {key: "opc", label: "Op cache promašaji %", title: helpOf("OP_CACHE_64B_FETCH_MISS_RATIO") + " (iz sesije inst_access)", num: true, wrap: true, d: 1, heat: true,
     get: (r) => { const x = fe("OP_CACHE_64B_FETCH_MISS_RATIO")(r); return x == null ? null : 100 * x; }},
    {key: "icm", label: "Promašaji keša instrukcija /1000", title: helpOf("IC_MISSES (PTI)") + " (iz sesije inst_access)", num: true, wrap: true, heat: true, get: fe("IC_MISSES (PTI)")},
    {key: "itlb", label: "ITLB promašaji /1000", title: helpOf("L1_ITLB_MISSES (PTI)") + " (iz sesije inst_access)", num: true, wrap: true, heat: true, get: fe("L1_ITLB_MISSES (PTI)")});
  const rows = s.functions.filter((f) => f.share >= state.min).map((f) => rowOf(s, f));
  drawTable("uarchTable", cols, rows, {sort: "share", pinned: [rowAgg(s, s.functions, "Cijela sesija (sve funkcije)")],
    onPick: (name) => openFunction(name, "pmc")});
}

// ---------- Cache and memory ----------
function cacheSizesHtml(w, s) {
  const info = (DATA.infos.find((i) => i.group === s.group) || {}).text || "";
  const kib = (label) => { const m = info.match(new RegExp(label + ":\\s*(\\d+) KiB")); return m ? +m[1] : null; };
  const l1 = kib("L1 data"), l2 = kib("L2 unified"), l3 = kib("L3 unified");
  if (!w.grid || l1 == null || l2 == null || l3 == null) return "";
  // One field: (n+2)^3 cells with the ghost layer, each row rounded up to 8 floats (Field3D), 4 bytes per float.
  const n = w.grid + 2, bytes = Math.ceil(n / 8) * 8 * n * n * 4, fieldKib = bytes / 1024;
  const fits = fieldKib <= l1 ? "staje u L1" : fieldKib <= l2 ? "staje u L2, ne u L1" : fieldKib <= l3 ? "staje u L3, ne u L2" : "ne staje ni u L3";
  return `<p><b>Veličina podataka prema kešu.</b> Keš ovog jezgra: L1 ${l1} KiB, L2 ${l2} KiB, L3 ${(l3 / 1024).toFixed(0)} MiB (L3 dijele sva jezgra).
    Jedno polje mreže ${w.grid}³ (s ghost slojem) ima ${(bytes / 1048576).toFixed(2)} MiB: <b>${fits}</b>. Jezgra čitaju i pišu više polja odjednom
    (Jacobi tri), pa je stvarni radni skup nekoliko puta veći.</p>`;
}
function renderCache() {
  const w = W(), s = ses(w, "pmc");
  if (!s) { el("cacheBody").style.display = "none"; if (!el("cacheMissing")) el("cacheBody").insertAdjacentHTML("beforebegin", `<div id="cacheMissing">${missing("hardverskim brojačima (assess_ext)")}</div>`); return; }
  if (el("cacheMissing")) el("cacheMissing").remove();
  el("cacheBody").style.display = "";
  el("cacheMin").value = state.min;
  el("cacheSizes").innerHTML = cacheSizesHtml(w, s);
  const a = (n) => agg(s, s.functions, n);
  el("cacheTiles").innerHTML = tile("L1 promašaji", fmt(a("%L1_DC_MISSES"), 1) + " %", fmt(a("L1_DC_ACCESSES (PTI)"), 0) + " pristupa memoriji na 1000 instrukcija")
    + REFILLS.filter(([n]) => n.indexOf("EXTERNAL") < 0).map(([n, label]) => tile("Punjenja L1 " + label, fmt(a(n), 2), "na 1000 instrukcija, cijela sesija")).join("")
    + tile("CPI", fmt(a("CPI"), 2), "ciklusa po instrukciji, cijela sesija");
  el("cacheLegend").innerHTML = "Punjenja L1: " + REFILLS.map(([, label, color]) => `<span><span class="dot" style="background:${color}"></span>${esc(label)}</span>`).join("");
  const refillSum = (r) => REFILLS.reduce((t, [n]) => t + (r.m[n] || 0), 0);
  const rows = s.functions.filter((f) => f.share >= state.min).map((f) => {
    const r = rowOf(s, f);
    r.ramShare = eventShare(s, f, "L1_DEMAND_DC_REFILLS_LOCAL_DRAM (PTI)");
    r.l3Share = eventShare(s, f, "L1_DEMAND_DC_REFILLS_LOCAL_CACHE (PTI)");
    r.l2Share = eventShare(s, f, "L1_DEMAND_DC_REFILLS_LOCAL_L2 (PTI)");
    return r;
  });
  const maxSum = Math.max(1e-9, ...rows.map(refillSum));
  const cols = [{key: "name", label: "Funkcija", get: (r) => r.name.toLowerCase(), html: (r) => esc(r.name)},
    {key: "share", label: "udio ciklusa %", num: true, wrap: true, get: (r) => r.share},
    metricCol("CPI", "CPI", {heat: true}),
    metricCol("L1_DC_ACCESSES (PTI)", "Pristupi memoriji /1000", {d: 0}),
    metricCol("%L1_DC_MISSES", "L1 promašaji %", {d: 1, heat: true}),
    {key: "refills", label: "Punjenja L1 /1000 po izvoru", title: "Zbir punjenja L1 na 1000 instrukcija, podijeljen po tome odakle je linija keša donesena.", get: refillSum,
     html: (r) => r._head ? "" : `<div style="min-width:150px"><div class="stack thin" style="width:${Math.max(1, 100 * refillSum(r) / maxSum)}%">`
       + REFILLS.map(([n, label, color]) => (r.m[n] || 0) > 0 ? `<div style="flex:${r.m[n]} 1 0;background:${color}" title="${esc(label)}: ${fmt(r.m[n], 2)}"></div>` : "").join("") + "</div></div>"},
    metricCol("L1_DEMAND_DC_REFILLS_LOCAL_L2 (PTI)", "iz L2 /1000", {heat: true}),
    metricCol("L1_DEMAND_DC_REFILLS_LOCAL_CACHE (PTI)", "iz L3 /1000", {heat: true}),
    metricCol("L1_DEMAND_DC_REFILLS_LOCAL_DRAM (PTI)", "iz RAM-a /1000", {heat: true}),
    {key: "l3Share", label: "udio svih punjenja iz L3 %", title: "Koliki dio svih punjenja L1 iz L3 u ovoj sesiji pripada ovoj funkciji.", num: true, wrap: true, d: 1, heat: true, get: (r) => r.l3Share},
    {key: "ramShare", label: "udio svih punjenja iz RAM-a %", title: "Koliki dio svih punjenja L1 iz RAM-a u ovoj sesiji pripada ovoj funkciji.", num: true, wrap: true, d: 1, heat: true, get: (r) => r.ramShare},
    metricCol("MISALIGNED_LOADS (PTI)", "Čitanja preko linije keša /1000", {d: 0, heat: true})];
  drawTable("cacheTable", cols, rows, {sort: "share", pinned: [rowAgg(s, s.functions, "Cijela sesija (sve funkcije)")],
    onPick: (name) => openFunction(name, "pmc")});
}

// ---------- Instructions ----------
function renderAsm() {
  const w = W();
  state.asmKind = pickBasis(w, state.asmKind);
  const s = ses(w, state.asmKind);
  const fs = s.functions.filter((f) => f.ins.length).sort((a, b) => b.share - a.share);
  const f = fs.find((x) => x.name === state.fn) || fs[0];
  el("asmKind").innerHTML = ["time", "pmc", "fe"].filter((k) => ses(w, k)).map((k) => `<option value="${k}">${esc(KIND_NAME[k])}</option>`).join("");
  el("asmKind").value = state.asmKind; el("asmOrder").value = state.asmOrder;
  el("asmFn").innerHTML = fs.map((x) => `<option value="${esc(x.name)}">${esc(x.name)} (${fmt(x.share, 1)}%)</option>`).join("");
  const extra = s.detailMetrics || [];
  const shareLabel = state.asmKind === "time" ? "udio vremena" : "udio ciklusa";
  if (state.heat !== "share" && !(+state.heat < extra.length)) state.heat = "share";
  el("asmHeat").innerHTML = `<option value="share">${shareLabel}</option>` + extra.map((m, i) => `<option value="${i}">${esc(m)}</option>`).join("");
  el("asmHeat").value = state.heat;
  if (!f) { el("asmTitle").textContent = "uProf za ovu sesiju nije dao instrukcije."; el("asmClasses").innerHTML = ""; el("asmTable").innerHTML = ""; return; }
  state.fn = f.name; el("asmFn").value = f.name;
  el("asmTitle").innerHTML = `<b>${esc(f.name)}</b> ${tag(f.phase)}: ${fmt(f.share, 1)}% sesije <i>${esc(s.id)}</i>, ${f.ins.length} instrukcija s uzorcima. `
    + `Postoci ispod su dio <b>svih</b> uzoraka sesije, ne ove funkcije.`;

  const classes = {};
  for (const i of f.ins) classes[CLASSES[i.c]] = (classes[CLASSES[i.c]] || 0) + i.s;
  const list = Object.entries(classes).sort((a, b) => b[1] - a[1]).filter(([, v]) => v > 0);
  const top = list.length ? list[0][1] : 1;
  el("asmClasses").innerHTML = "<details><summary>Po vrsti instrukcije</summary><table style='max-width:640px'>" + list.map(([name, v]) =>
    `<tr><td>${esc(name)}</td><td style="width:50%"><div class="bar" style="width:${100 * v / top}%;background:var(--bar2)"></div></td><td class="num">${fmt(v, 2)}%</td></tr>`).join("") + "</table></details>";

  const hv = (i) => state.heat === "share" ? i.s : ((i.v || [])[+state.heat] || 0);
  const maxShare = Math.max(1e-9, ...f.ins.map((i) => i.s));
  // Counter ratios on instructions with very few samples are noise: colour only rows with at least 2% of the
  // hottest instruction's samples when a counter column is chosen.
  const coloured = (i) => state.heat === "share" || i.s >= 0.02 * maxShare;
  const maxHeat = Math.max(1e-9, ...f.ins.filter(coloured).map(hv));
  const first = parseInt(f.ins[0].a, 16), targets = new Map();
  for (const i of f.ins) {
    const m = i.t.match(/^j\w+\s+0x([0-9A-Fa-f]+)$/);
    if (m) { const to = parseInt(m[1], 16), at = parseInt(i.a, 16); if (to < at && to >= first) targets.set(to, at); }
  }
  let rows = `<thead><tr><th class="num">${shareLabel} %</th><th style="min-width:90px"></th><th>Adresa</th><th>Instrukcija (AT&amp;T: odredište je zadnje)</th><th>Vrsta</th><th>Petlja</th>`
    + extra.map((m, k) => `<th class="num wrap${String(k) === state.heat ? " sorted" : ""}" title="${esc(helpOf(m))}">${esc(m)}</th>`).join("") + "</tr></thead><tbody>";
  const ordered = state.asmOrder === "addr" ? f.ins : f.ins.slice().sort((a, b) => b.s - a.s).slice(0, 80);
  let expected = null;
  for (const i of ordered) {
    const at = parseInt(i.a, 16);
    if (state.asmOrder === "addr" && expected !== null && at !== expected) rows += `<tr class="gap"><td colspan="${6 + extra.length}">…</td></tr>`;
    expected = at + i.n;
    const jump = i.t.match(/^j\w+\s+0x([0-9A-Fa-f]+)$/);
    let loop = "";
    if (targets.has(at)) loop = "▸ početak petlje";
    if (jump && targets.get(parseInt(jump[1], 16)) === at) loop += (loop ? " · " : "") + "↑ skok unazad";
    const bg = coloured(i) && hv(i) > 0 ? ` style="background:${heat(hv(i) / maxHeat)}"` : "";
    rows += `<tr${i.s === maxShare ? ' id="asmHot"' : ""}><td class="num"${bg}>${fmt(i.s, 2)}</td><td${bg}><div class="bar" style="width:${100 * i.s / maxShare}%"></div></td>`
      + `<td${bg}><code>${esc(i.a)}</code></td><td${bg}><code>${esc(i.t)}</code></td><td class="small" style="white-space:nowrap">${esc(CLASSES[i.c])}</td><td class="small muted" style="white-space:nowrap">${loop}</td>`
      + extra.map((m, k) => `<td class="num">${fmt((i.v || [])[k], 2)}</td>`).join("") + "</tr>";
  }
  el("asmTable").innerHTML = rows + "</tbody>";
  // In address order the function starts with its cold prologue: bring the hottest instruction into view.
  const hot = el("asmHot"), box = el("asmTable").parentElement;
  box.scrollTop = (hot && state.asmOrder === "addr") ? Math.max(0, hot.offsetTop - box.clientHeight / 2) : 0;
}

// ---------- versions: data shared by "Ocjena" and "Poređenje verzija" ----------
// Version runs are the folders named <date>_ver_<version> (perf/uprof/collect.sh with VERSION=...): the same
// whole-step workload run by the naive M3 code, the auto-vectorized M3 code and the final solver. If there are
// several such sets, the newest one is used.
const VSET = WL.reduce((best, w) => (w.vset && (best === null || w.vset > best)) ? w.vset : best, null);
const VW = WL.filter((w) => VSET !== null && w.vset === VSET && w.stage === "all");
const findV = (version, grid) => VW.find((w) => w.version === version && w.grid === +grid) || null;
const vGrids = () => [...new Set(VW.map((w) => w.grid))].sort((a, b) => a - b);
const msOf = (s) => s ? parseFloat(s.workload.ms_po_ponavljanju) : NaN;
const infoText = (s) => (DATA.infos.find((i) => i.group === s.group) || {}).text || "";
const STEP_HZ = 60;   // one step advances the simulation by dt = 1/60 s (Solver/include/smoke/SmokeParams.h)
const RAM = "L1_DEMAND_DC_REFILLS_LOCAL_DRAM (PTI)", L3 = "L1_DEMAND_DC_REFILLS_LOCAL_CACHE (PTI)", L2 = "L1_DEMAND_DC_REFILLS_LOCAL_L2 (PTI)";

// Everything the two tabs need about one workload. Times come from the time-based session (the profiler slows
// the program least there), counters from the assess_ext session.
//   inst:  PROPORTIONAL to the instructions of one step. time = instructions x CPI / clock, so with the same clock
//          in both sessions, instructions ~ (ms per step) / CPI. Only ratios of it between versions are used.
//   instS: the same quantity estimated another way: samples of RETIRED_INST x (ms per step), i.e. samples per
//          step when both sessions sampled for the same time. On PC-2 the two estimates differ by up to 30%
//          (docs/plan/PROFILE.md, section 7), so the page shows both as a range.
function metricsOf(w) {
  const st = ses(w, "time"), sp = ses(w, "pmc"), timeBase = st || sp;
  const m = {w, st, sp, timeBase, ms: msOf(timeBase), msP: msOf(sp), phaseMs: {}, phase: {}};
  m.phaseShare = phaseShares(timeBase);
  for (const p of PHASES) m.phaseMs[p] = (m.phaseShare[p] || 0) / 100 * m.ms;
  const widths = [0, 0, 0, 0];
  for (const f of timeBase.functions) (f.vw || []).forEach((v, i) => { widths[i] += v; });
  m.covered = widths.reduce((a, b) => a + b, 0);
  m.width = widths.map((v) => m.covered ? 100 * v / m.covered : 0);
  if (!sp) return m;
  const a = (n, fs) => agg(sp, fs || sp.functions, n);
  Object.assign(m, {cpi: a("CPI"), misp: a("%RETIRED_BR_INST_MISP"), br: a("RETIRED_BR_INST (PTI)"), l1: a("%L1_DC_MISSES"),
    acc: a("L1_DC_ACCESSES (PTI)"), l2: a(L2), l3: a(L3), ram: a(RAM), mis: a("MISALIGNED_LOADS (PTI)")});
  m.inst = m.cpi ? m.msP / m.cpi : null;
  m.instS = a("RETIRED_INST") * m.msP;
  const cycles = phaseShares(sp);
  for (const p of PHASES) {
    const fs = sp.functions.filter((f) => f.phase === p);
    if (!fs.length) continue;
    const cpi = a("CPI", fs);
    m.phase[p] = {cpi, inst: cpi ? (cycles[p] || 0) / 100 * m.msP / cpi : null};
  }
  return m;
}
// Events of one step, in the same arbitrary unit as m.inst: rate per 1000 instructions x instructions.
const perStep = (m, rate) => (rate == null || m.inst == null) ? null : rate * m.inst;
const ratio = (x, y) => (x == null || y == null || !isFinite(x) || !isFinite(y) || y === 0) ? null : x / y;
const times = (x) => fmt(x, x >= 10 ? 1 : 2) + " puta";
// How many times fewer instructions per step b runs than a: the two estimates, smaller first.
function instRange(a, b) {
  const list = [ratio(a.inst, b.inst), ratio(a.instS, b.instS)].filter((x) => x != null).sort((x, y) => x - y);
  return list.length ? {lo: list[0], hi: list[list.length - 1], text: list.length > 1 && fmt(list[0], 1) !== fmt(list[1], 1) ? `${fmt(list[0], 1)} do ${fmt(list[1], 1)}` : fmt(list[0], 1)} : null;
}
// r = naive / this, for a quantity where less is better. Above 1: this version is better.
function verdict(r, better, worse, tail) {
  if (r == null || !isFinite(r) || r <= 0) return "";
  if (Math.abs(r - 1) < 0.005) return `<span class="verdict">isto kao naivna verzija</span>`;
  return r > 1 ? `<span class="verdict good">${times(r)} ${better} ${tail}</span>` : `<span class="verdict bad">${times(1 / r)} ${worse} ${tail}</span>`;
}
function seg(id, items, current, pick) {
  el(id).innerHTML = items.map(([value, label]) => `<button data-v="${esc(value)}" class="${String(value) === String(current) ? "on" : ""}">${esc(label)}</button>`).join("");
  el(id).querySelectorAll("button").forEach((b) => b.onclick = () => pick(b.dataset.v));
}
function widthStack(m) {
  return `<div class="stack">` + m.width.map((v, i) => v > 0.05 ? `<div style="width:${v}%;background:${WIDTH_COLORS[i]}" title="${esc(DATA.widths[i])}: ${fmt(v, 1)}%"></div>` : "").join("") + "</div>";
}
// Size of the three fields one Jacobi iteration touches, against the last-level cache of this machine.
function jacobiFit(w, s) {
  const match = infoText(s).match(/L3 unified:\s*(\d+) KiB/);
  if (!w.grid || !match) return null;
  const n = w.grid + 2, mib = 3 * Math.ceil(n / 8) * 8 * n * n * 4 / 1048576, l3 = +match[1] / 1024;
  return {mib, l3, fits: mib <= l3};
}

// ---------- Grade ("Ocjena") ----------
const gradeList = () => VW.length ? VW : WL.filter((w) => w.stage === "all");
// The tab describes one whole-step workload. If the header points at something else (one stage, an obstacle
// run), move to the final version at 64^3, or to the first workload the tab can describe.
function snapGrade() {
  const list = gradeList();
  if (list.length && !list.includes(W())) state.w = WL.indexOf(findV(W().version, W().grid) || findV("m4", 64) || list[0]);
}
const card = (title, question, body, plain) => `<div class="card"><h3>${title}</h3><p class="q">${question}</p>${body}`
  + `<div class="plain"><b>Jednostavno rečeno.</b> ${plain}</div></div>`;
const big = (value, unit, sub) => `<div class="big">${value} <span class="unit">${unit}</span></div>` + (sub ? `<div class="small muted">${sub}</div>` : "");
const notMeasured = (what) => `<div class="big">nije mjereno</div><div class="small muted">${what}</div>`;

function renderGrade() {
  const list = gradeList();
  if (!list.length) { el("grHead").innerHTML = missing("cijelim korakom simulacije"); el("grCards").innerHTML = ""; return; }
  const w = W(), m = metricsOf(w), sp = m.sp;
  if (VW.length) {
    seg("grVer", VERSIONS.filter((v) => findV(v, w.grid)).map((v) => [v, VSHORT[v]]), w.version, (v) => { state.w = WL.indexOf(findV(v, w.grid)); render(); });
    seg("grGrid", vGrids().filter((g) => findV(w.version, g)).map((g) => [g, g + "³"]), w.grid, (g) => { state.w = WL.indexOf(findV(w.version, g)); render(); });
  } else {
    el("grVer").innerHTML = `<button class="on">${esc(VSHORT[w.version] || w.version)}</button>`;
    seg("grGrid", list.map((x) => [WL.indexOf(x), x.grid + "³ · " + x.run]), state.w, (i) => { state.w = +i; render(); });
  }
  const baseW = findV("m3", w.grid), isBase = baseW === w;
  const bm = baseW && !isBase ? metricsOf(baseW) : null;
  const S = bm ? ratio(bm.ms, m.ms) : null;
  const instR = bm ? ratio(bm.inst, m.inst) : null;       // how many times fewer instructions per step than naive
  const cpiR = bm ? ratio(bm.cpi, m.cpi) : null;          // above 1: fewer cycles per instruction than naive
  const range = bm ? instRange(bm, m) : null;
  const threadsMatch = infoText(m.timeBase).match(/hardware_threads:\s*(\d+)/);
  const threads = threadsMatch ? +threadsMatch[1] : null;
  const fit = jacobiFit(w, m.timeBase);
  const top = PHASES.filter((p) => (m.phaseShare[p] || 0) > 0.5).sort((a, b) => m.phaseShare[b] - m.phaseShare[a]);
  const official = (base) => ((DATA.official[base] || {})[String(w.grid)] || {}).whole_step;

  // ----- headline and short assessment -----
  let head = `<p class="lead"><b>${esc(VLONG[w.version] || w.version)}</b>, mreža ${w.grid}³ (${(w.grid ** 3).toLocaleString("de-DE")} ćelija): `
    + `jedan korak simulacije traje <b>${fmt(m.ms, 2)} ms</b>, što je <b>${fmt(1000 / m.ms, 0)} koraka u sekundi</b>.`
    + (S ? ` To je <b>${times(S)} ${S >= 1 ? "brže" : "sporije"}</b> od naivne verzije na istoj mreži.` : isBase ? " Ovo je polazna verzija: s njom se porede ostale." : "") + "</p>";
  const points = [];
  if (S && instR && cpiR) {
    points.push(`<b>Zašto je ${S >= 1 ? "brža" : "sporija"}.</b> Za isti korak izvrši ${instR >= 1 ? "oko " + range.text + " puta manje" : times(1 / instR) + " više"} instrukcija nego naivna (procjena), `
      + (cpiR >= 1 ? `a svaka instrukcija u prosjeku traje ${times(cpiR)} kraće` : `ali svaka instrukcija u prosjeku traje ${times(1 / cpiR)} duže`)
      + ` (CPI ${fmt(bm.cpi)} → ${fmt(m.cpi)}). Brzinu određuje proizvod ta dva broja.`);
  }
  if (m.covered) points.push(`<b>Paralelizam unutar jedne niti (SIMD).</b> ${fmt(m.width[0], 0)} % vremena ide na instrukcije koje obrađuju 8 brojeva odjednom`
    + (bm ? `; kod naivne verzije ${fmt(bm.width[0], 0)} %.` : "."));
  if (threads) points.push(`<b>Paralelizam nitima.</b> Program koristi 1 od ${threads} niti procesora. Paralelizacija nitima nije rađena (odluka D8), pa je to neiskorišten prostor, isti u svim verzijama.`);
  if (sp && fit) points.push(`<b>Memorija.</b> Tri polja koja Jacobi koristi odjednom imaju ${fmt(fit.mib, 1)} MiB, a L3 keš ${fmt(fit.l3, 0)} MiB: `
    + (fit.fits ? "podaci staju u keš." : "podaci <b>ne staju</b> u keš, pa ih procesor stalno dovlači iz RAM-a.")
    + ` Na 1000 instrukcija procesor ${fmt(m.ram, 2)} puta čeka podatak iz RAM-a.`);
  if (top.length) points.push(`<b>Gdje ide vrijeme.</b> Najviše na: ${top.slice(0, 3).map((p) => `${esc(p)} (${fmt(m.phaseShare[p], 0)} %)`).join(", ")}. Najveća faza je mjesto gdje bi sljedeće ubrzanje najviše vrijedilo.`);
  el("grHead").innerHTML = head + "<ul>" + points.map((p) => `<li>${p}</li>`).join("") + "</ul>";

  // ----- cards -----
  const cards = [];
  const perSecond = 1000 / m.ms, o0 = official("m3"), o1 = official("m3auto");
  cards.push(card("Brzina", "Koliko brzo računar izračuna jedan korak simulacije?",
    big(fmt(perSecond, 0), "koraka u sekundi", `${fmt(m.ms, 2)} ms po koraku (profilna gradnja, pod profilerom)`)
    + (isBase ? `<span class="verdict">polazna verzija</span>` : verdict(S, "brže", "sporije", "od naivne verzije"))
    + `<p class="small">Jedan korak pomjera simulaciju za 1/${STEP_HZ} s. Da dim teče u stvarnom vremenu treba najmanje ${STEP_HZ} koraka u sekundi; ova verzija daje `
    + (perSecond >= STEP_HZ ? `${times(perSecond / STEP_HZ)} toliko` : `${fmt(100 * perSecond / STEP_HZ, 0)} % toga`) + ". Računa se samo simulacija, bez crtanja.</p>"
    + (o0 ? `<p class="small muted">Službeno mjerenje (obična gradnja, protokol D10, ${esc(o0.date)}): finalna verzija je ${fmt(o0.S, 2)}x brža od naivne`
      + (o1 ? ` i ${fmt(o1.S, 2)}x brža od auto-vektorizirane` : "") + ` na ${w.grid}³.</p>` : ""),
    "Ovo je broj sličica dima koje računar izračuna u jednoj sekundi. Veći broj znači da ista simulacija teče glatko, ili da za isto vrijeme stane veća i detaljnija."));

  cards.push(card("Paralelizam: niti procesora", "Koliko jezgara procesora program zapošljava?",
    (threads ? big("1 od " + threads, "niti") + `<div class="boxes">${Array.from({length: threads}, (_, i) => `<span class="${i === 0 ? "on" : ""}"></span>`).join("")}</div>` : big("1", "nit"))
    + `<span class="verdict">nije urađeno, po planu (odluka D8)</span>`
    + (fit && !fit.fits ? `<p class="small">Na ovoj mreži Jacobi čeka memoriju (kartica "Promašaji keša"). Memoriju dijele sva jezgra, pa dodatne niti u tom dijelu možda neće dati srazmjerno ubrzanje. To <b>nije izmjereno</b>.</p>` : ""),
    "Procesor ovog računara može raditi više poslova istovremeno, kao više radnika u radionici. Ovaj program zapošljava jednog. To nije propust: prvo se jedan radnik nauči da radi što brže, pa se tek onda posao dijeli. Ovaj broj je isti u svim verzijama."));

  cards.push(card("Paralelizam: unutar jedne niti (SIMD)", "Koliko brojeva jedna instrukcija obradi odjednom?",
    (m.covered ? big(fmt(m.width[0], 0) + " %", "vremena u instrukcijama za 8 brojeva odjednom") + `<div style="margin:8px 0">${widthStack(m)}</div>`
      + `<div class="small">${m.width.map((v, i) => `<div><span class="dot" style="background:${WIDTH_COLORS[i]}"></span>${esc(WIDTH_SHORT[i])}: ${fmt(v, 1)} %</div>`).join("")}</div>`
      + (bm ? `<span class="verdict ${m.width[0] >= bm.width[0] ? "good" : "bad"}">naivna verzija: ${fmt(bm.width[0], 0)} %</span>` : isBase ? `<span class="verdict">polazna verzija</span>` : "")
      : notMeasured("uProf za ovu sesiju nije dao instrukcije")),
    "Obična instrukcija sabere dva broja. Vektorska (SIMD) instrukcija u istom taktu sabere osam parova, kao kasa koja jednim potezom skenira osam artikala. Što veći dio vremena program provodi u takvim instrukcijama, to bolje koristi jedno jezgro."));

  cards.push(card("Količina posla", "Koliko instrukcija treba za jedan korak?",
    (instR ? big(range.lo >= 1 ? range.text + " puta" : times(1 / instR), (range.lo >= 1 ? "manje" : "više") + " instrukcija po koraku nego naivna verzija",
        "procjena na dva načina; broj instrukcija nije izbrojan direktno")
      + `<span class="verdict ${range.lo >= 1 ? "good" : "bad"}">${range.lo >= 1 ? "manje" : "više"} instrukcija od naivne verzije</span>`
      : isBase ? big("1.00", "polazna vrijednost") + `<span class="verdict">polazna verzija</span>` : notMeasured("treba sesija s brojačima za ovu i za naivnu verziju")),
    "Do istog rezultata može se doći dužim ili kraćim putem. Optimizirana verzija isti račun obavi s manje instrukcija, najviše zato što jedna vektorska instrukcija zamijeni osam običnih. Rezultat simulacije je bit po bit isti."));

  cards.push(card("Efikasnost procesora", "Koliko posla procesor završi u jednom taktu?",
    (sp ? big(fmt(1 / m.cpi, 2), "instrukcija po taktu", `CPI = ${fmt(m.cpi, 2)} ciklusa po instrukciji (manje je bolje)`)
      + (isBase ? `<span class="verdict">polazna verzija</span>` : verdict(cpiR, "manje ciklusa po instrukciji", "više ciklusa po instrukciji", "od naivne verzije"))
      + (cpiR && cpiR < 1 && S > 1 ? `<p class="small">To ne znači da je verzija sporija. Instrukcija je manje (kartica "Količina posla") i svaka radi više posla, pa duže traje. Brzinu određuje proizvod ta dva broja.</p>` : "")
      : notMeasured("nema sesije s hardverskim brojačima")),
    "Procesor radi u taktovima, kao metronom. U jednom taktu može završiti i nekoliko instrukcija, ako ne mora čekati podatke. Kad čeka, taktovi prolaze uprazno."));

  let cacheVerdict = "";
  if (bm && sp && bm.sp) {
    const useRam = m.ram >= 0.005 && bm.ram >= 0.005;
    const mine = useRam ? perStep(m, m.ram) : perStep(m, m.l2 + m.l3 + m.ram), base = useRam ? perStep(bm, bm.ram) : perStep(bm, bm.l2 + bm.l3 + bm.ram);
    cacheVerdict = verdict(ratio(base, mine), useRam ? "manje čekanja na RAM po koraku" : "manje čekanja na podatke po koraku",
      useRam ? "više čekanja na RAM po koraku" : "više čekanja na podatke po koraku", "od naivne verzije");
  }
  cards.push(card("Promašaji keša", "Koliko često procesor čeka na podatke?",
    (sp ? big(fmt(100 - m.l1, 1) + " %", "pristupa memoriji nađeno odmah (u L1 kešu)",
        `Na 1000 instrukcija procesor čeka: ${fmt(m.l2, 1)} puta na L2, ${fmt(m.l3, 2)} puta na L3 i ${fmt(m.ram, 2)} puta na RAM.`)
      + (isBase ? `<span class="verdict">polazna verzija</span>` : cacheVerdict)
      + (fit ? `<p class="small">Tri polja Jacobija: ${fmt(fit.mib, 1)} MiB; L3 keš: ${fmt(fit.l3, 0)} MiB. ${fit.fits ? "Staju u keš." : "<b>Ne staju u keš.</b>"}</p>` : "")
      : notMeasured("nema sesije s hardverskim brojačima")),
    "Keš je mali i brz radni sto odmah uz procesor; RAM je veliko skladište dalje od njega. Podatak koji je već na stolu stiže odmah. Za svaki koji nije, procesor čeka, a najduže kad mora po njega u RAM."));

  cards.push(card("Grananja", "Koliko često procesor pogrešno pogodi kuda kod ide?",
    (sp ? big(fmt(m.misp, 2) + " %", "grananja pogrešno predviđeno", `${fmt(m.br, 0)} grananja na 1000 instrukcija`)
      + (isBase ? `<span class="verdict">polazna verzija</span>`
        : bm && bm.sp ? verdict(ratio(perStep(bm, bm.misp * bm.br), perStep(m, m.misp * m.br)), "manje pogrešnih predviđanja po koraku", "više pogrešnih predviđanja po koraku", "od naivne verzije") : "")
      : notMeasured("nema sesije s hardverskim brojačima")),
    "Na svakom \"ako\" u programu procesor unaprijed pogađa kojim putem će kod krenuti, da ne bi čekao. Kad pogodi, ne gubi ništa. Kad promaši, baca započeti posao i kreće ispočetka."));

  cards.push(card("Gdje ide vrijeme", "Koji dio koraka troši najviše?",
    stackHtml(m.timeBase) + `<div class="small" style="margin-top:8px">${top.slice(0, 5).map((p) =>
      `<div><span class="dot" style="background:${PHASE_COLORS[p]}"></span>${esc(p)}: ${fmt(m.phaseShare[p], 1)} % (${fmt(m.phaseMs[p], 2)} ms)</div>`).join("")}</div>`,
    "Jedan korak simulacije ima nekoliko poslova: pomjeranje dima strujom zraka (advekcija), računanje pritiska (Jacobi), sile i rubove kutije. Najšira traka je posao koji troši najviše vremena."));
  el("grCards").innerHTML = cards.join("");
}

// ---------- Compare versions ----------
function renderCompare() {
  if (!VW.length) {
    el("cmpBody").innerHTML = `<p class="warn">Među učitanim sesijama nema snimljenih verzija. Snimaju se sa <code>VERSION=m3 STAGES=all perf/uprof/collect.sh ver_m3_tbp tbp</code> `
      + `(isto za <code>m3auto</code> i <code>m4</code>, i za <code>assess_ext</code> s oznakom <code>_ebp</code>); opis je u docs/plan/PROFILE.md, odjeljak 7.</p>`;
    return;
  }
  const grids = vGrids();
  if (!grids.includes(+state.cg)) state.cg = grids[0];
  const versions = VERSIONS.filter((v) => findV(v, state.cg));
  if (!versions.includes(state.ca)) state.ca = versions[0];
  if (!versions.includes(state.cb)) state.cb = versions[versions.length - 1];
  seg("cmpGrid", grids.map((g) => [g, g + "³"]), state.cg, (g) => { state.cg = +g; render(); });
  const options = versions.map((v) => `<option value="${v}">${esc(VLONG[v])}</option>`).join("");
  el("cmpA").innerHTML = options; el("cmpB").innerHTML = options;
  el("cmpA").value = state.ca; el("cmpB").value = state.cb;
  const wa = findV(state.ca, state.cg), wb = findV(state.cb, state.cg);
  const ma = metricsOf(wa), mb = metricsOf(wb);
  const S = ratio(ma.ms, mb.ms), instR = ratio(ma.inst, mb.inst), cpiR = ratio(mb.cpi, ma.cpi);
  const off = state.cb === "m4" ? (DATA.official[state.ca] || {})[String(state.cg)] : null;
  const range = instRange(ma, mb);
  const l1R = ma.sp && mb.sp ? ratio(perStep(mb, mb.l1 * mb.acc), perStep(ma, ma.l1 * ma.acc)) : null;

  // Words for a ratio A / B, correct for either order of A and B.
  // The same number of decimals as the printed ratio decides "equal", so the words never contradict the number.
  const isOne = (r) => fmt(r, 2) === "1.00";
  const fewer = (r) => isOne(r) ? "isto" : r > 1 ? `${fmt(r, 2)} puta manje` : `${fmt(1 / r, 2)} puta više`;
  const speedWords = (r) => r == null ? "" : isOne(r) ? "B je jednako brza kao A" : r > 1 ? `B je ${fmt(r, 2)} puta brža od A` : `B je ${fmt(1 / r, 2)} puta sporija od A`;
  const instWords = (r) => r == null ? "" : isOne(r) ? "B izvrši isto instrukcija kao A" : r > 1 ? `B izvrši ${fmt(r, 2)} puta manje instrukcija` : `B izvrši ${fmt(1 / r, 2)} puta više instrukcija`;
  const ratioCell = (r) => r == null ? "–" : `<span class="${isOne(r) ? "muted" : r > 1 ? "neg" : "pos"}">${fmt(r, 2)}</span>`;

  el("cmpTiles").innerHTML = tile("A: " + esc(VSHORT[state.ca]), fmt(ma.ms, 2) + " ms", `po koraku; ${fmt(1000 / ma.ms, 0)} koraka u sekundi`)
    + tile("B: " + esc(VSHORT[state.cb]), fmt(mb.ms, 2) + " ms", `po koraku; ${fmt(1000 / mb.ms, 0)} koraka u sekundi`)
    + tile("Odnos vremena S = A ÷ B", fmt(S, 2) + "x", esc(speedWords(S)) + " (pod profilerom)")
    + (off && off.whole_step ? tile("Službeno S", fmt(off.whole_step.S, 2) + "x", `obična gradnja, protokol D10, ${esc(off.whole_step.date)} (PERF_LOG.md, odjeljak 7)`)
      : tile("Službeno S", "nije mjereno", "za ovaj par verzija nema zbirnog unosa u perf/results.csv"))
    + tile("Instrukcije po koraku A ÷ B", range ? range.text + "x" : "–", range ? (range.lo === range.hi || (range.lo > 1) === (range.hi > 1)
        ? esc(instWords(instR)) + (ratio(ma.instS, mb.instS) != null ? "; druga procjena: " + esc(fewer(ratio(ma.instS, mb.instS))) : "")
        : "dvije procjene se ne slažu u smjeru") + " (nije direktno izbrojano)" : "nema sesija s brojačima za obje verzije")
    + tile("CPI: A → B", ma.cpi ? `${fmt(ma.cpi, 2)} → ${fmt(mb.cpi, 2)}` : "–", "ciklusa po instrukciji");
  el("cmpWhy").innerHTML = (instR && cpiR) ? `<p class="note"><b>Odakle dolazi razlika.</b> Vrijeme koraka je broj instrukcija puta ciklusa po instrukciji. `
    + `B izvrši ${fewer(instR)} instrukcija, a svaka u prosjeku traje ${isOne(cpiR) ? "jednako" : cpiR > 1 ? fmt(cpiR, 2) + " puta duže" : fmt(1 / cpiR, 2) + " puta kraće"}. `
    + `Zajedno to daje S = ${fmt(instR / cpiR, 2)}x u sesijama s brojačima (vremensko uzorkovanje daje ${fmt(S, 2)}x).`
    + (instR > 1 && cpiR > 1 && l1R ? ` Instrukcija je manje jer jedna vektorska instrukcija radi posao više skalarnih. Podataka koje korak mora proći ima isto `
      + `(L1 promašaja po koraku B ima ${fmt(l1R, 2)} puta koliko A), pa na svaku od manjeg broja instrukcija dolazi više čekanja na memoriju.` : "")
    + ` <span class="muted">Broj instrukcija nije izbrojan direktno: gornji odnos je izračunat iz vremena i CPI-ja, a iz broja uzoraka instrukcija izlazi `
    + `${fewer(ratio(ma.instS, mb.instS))}. Razlika između dvije procjene nije objašnjena.</span></p>` : "";

  // Phases: time of the phase in one step, for both versions.
  // Phases under 0.5% of the step in both versions have too few samples for a ratio.
  const phases = PHASES.filter((p) => Math.max(ma.phaseShare[p] || 0, mb.phaseShare[p] || 0) >= 0.5);
  const maxMs = Math.max(1e-9, ...phases.map((p) => Math.max(ma.phaseMs[p], mb.phaseMs[p])));
  const row = (name, ta, tb, pa, pb, extra) => Object.assign({name, ta, tb, s: ratio(ta, tb), ca: pa ? pa.cpi : null, cb: pb ? pb.cpi : null,
    ir: pa && pb ? ratio(pa.inst, pb.inst) : null}, extra || {});
  const duo = {key: "duo", label: "A / B", html: (r) => r._head ? "" : `<div class="duo"><div style="width:${100 * r.ta / maxMs}%;background:var(--bar2)" title="A: ${fmt(r.ta, 3)} ms"></div>`
    + `<div style="width:${100 * r.tb / maxMs}%;background:var(--bar)" title="B: ${fmt(r.tb, 3)} ms"></div></div>`};
  drawTable("cmpPhases", [{key: "name", label: "Faza", get: (r) => r.name, html: (r) => r._head ? esc(r.name) : tag(r.name)}, duo,
    {key: "ta", label: "A ms po koraku", num: true, d: 3, get: (r) => r.ta}, {key: "tb", label: "B ms po koraku", num: true, d: 3, get: (r) => r.tb},
    {key: "s", label: "S = A ÷ B", title: "Vrijeme faze u A podijeljeno vremenom u B. Iznad 1: B je brža u ovoj fazi. Ispod 1: B je sporija.", num: true, get: (r) => r.s, html: (r) => ratioCell(r.s)},
    {key: "ir", label: "Instrukcije A ÷ B (procjena)", title: "Broj instrukcija faze u A podijeljen brojem u B. Iznad 1: B izvrši manje instrukcija. Ispod 1: B izvrši više. Procjena iz vremena i CPI-ja sesija s brojačima; nije direktno izbrojano.", num: true, wrap: true, get: (r) => r.ir, html: (r) => ratioCell(r.ir)},
    {key: "ca", label: "A CPI", title: helpOf("CPI"), num: true, get: (r) => r.ca}, {key: "cb", label: "B CPI", title: helpOf("CPI"), num: true, get: (r) => r.cb}],
    phases.map((p) => row(p, ma.phaseMs[p], mb.phaseMs[p], ma.phase[p], mb.phase[p])),
    {sort: "ta", pinned: [row("Cijeli korak", ma.ms, mb.ms, {cpi: ma.cpi, inst: ma.inst}, {cpi: mb.cpi, inst: mb.inst}, {_head: true, _noheat: true})]});

  // Register width: data parallelism inside one thread.
  el("cmpWidth").innerHTML = "<table><tr><th>Verzija</th><th style='width:40%'></th>" + WIDTH_SHORT.map((n) => `<th class="num wrap">${esc(n)} %</th>`).join("") + "</tr>"
    + [["A", state.ca, ma], ["B", state.cb, mb]].map(([letter, v, m]) => `<tr><td>${letter}: ${esc(VSHORT[v])}</td><td>${widthStack(m)}</td>`
      + m.width.map((x) => `<td class="num">${fmt(x, 1)}</td>`).join("") + "</tr>").join("") + "</table>"
    + `<div class="legend">${WIDTH_SHORT.map((n, i) => `<span><span class="dot" style="background:${WIDTH_COLORS[i]}"></span>${esc(n)}</span>`).join("")}</div>`;

  // Counters. Rates per 1000 instructions are not comparable between versions that run a different number of
  // instructions, so the last column gives the same event count per step, B relative to A.
  if (ma.sp && mb.sp) {
    // No ratio when A prints as zero: a ratio to (almost) nothing says nothing.
    const line = (label, a, b, stepA, stepB, d, help) => { const r = (a != null && parseFloat(fmt(a, d)) === 0) ? null : ratio(stepB, stepA);
      return `<tr title="${esc(help || "")}"><td>${label}</td><td class="num">${fmt(a, d)}</td><td class="num">${fmt(b, d)}</td>`
        + `<td class="num">${r == null ? "–" : `<span class="${r < 0.995 ? "neg" : r > 1.005 ? "pos" : "muted"}">${fmt(r, 2)}x</span>`}</td></tr>`; };
    const pti = (label, name, d) => { const a = agg(ma.sp, ma.sp.functions, name), b = agg(mb.sp, mb.sp.functions, name);
      return line(label, a, b, perStep(ma, a), perStep(mb, b), d, helpOf(name)); };
    el("cmpCounters").innerHTML = "<tr><th>Brojač</th><th class='num'>A</th><th class='num'>B</th><th class='num wrap' title='Isti događaj prebrojan po jednom koraku simulacije, B podijeljeno s A. Ispod 1: B ih ima manje (zeleno). Iznad 1: B ih ima više (crveno).'>po koraku, B ÷ A (procjena)</th></tr>"
      + line("Ciklusi procesora (vrijeme koraka u sesiji s brojačima)", ma.msP, mb.msP, ma.msP, mb.msP, 2, "ms po koraku u sesiji assess_ext")
      + line("Instrukcije (relativno)", null, null, ma.inst, mb.inst, 2, "Izračunato iz vremena koraka i CPI-ja.")
      + line("CPI (ciklusa po instrukciji)", ma.cpi, mb.cpi, null, null, 2, helpOf("CPI"))
      + pti("Grananja na 1000 instrukcija", "RETIRED_BR_INST (PTI)", 1)
      + line("Pogrešno predviđena grananja %", ma.misp, mb.misp, perStep(ma, ma.misp * ma.br), perStep(mb, mb.misp * mb.br), 2, helpOf("%RETIRED_BR_INST_MISP"))
      + pti("Pristupi memoriji na 1000 instrukcija", "L1_DC_ACCESSES (PTI)", 0)
      + line("L1 promašaji %", ma.l1, mb.l1, perStep(ma, ma.l1 * ma.acc), perStep(mb, mb.l1 * mb.acc), 1, helpOf("%L1_DC_MISSES"))
      + pti("Punjenja L1 iz L2 na 1000 instrukcija", L2, 2) + pti("Punjenja L1 iz L3 na 1000 instrukcija", L3, 2) + pti("Punjenja L1 iz RAM-a na 1000 instrukcija", RAM, 2)
      + pti("Čitanja preko linije keša na 1000 instrukcija", "MISALIGNED_LOADS (PTI)", 0);
  } else el("cmpCounters").innerHTML = `<tr><td>${missing("hardverskim brojačima za obje verzije")}</td></tr>`;

  // Hottest functions of each version, side by side.
  const fnTable = (id, w, m) => {
    const rows = m.timeBase.functions.slice().sort((x, y) => y.share - x.share).slice(0, 10).map((f) => {
      const r = rowOf(m.timeBase, f), p = byName(m.sp, f.name);
      r.ms = f.share / 100 * m.ms; r.cpi = p ? val(m.sp, p, "CPI") : null;
      return r;
    });
    drawTable(id, [{key: "name", label: "Funkcija", get: (r) => r.name.toLowerCase(), html: (r) => esc(r.name)}, {key: "phase", label: "Faza", html: (r) => tag(r.phase)},
      {key: "ms", label: "ms po koraku", num: true, d: 3, get: (r) => r.ms}, {key: "share", label: "udio %", num: true, d: 1, get: (r) => r.share},
      {key: "cpi", label: "CPI", title: helpOf("CPI"), num: true, heat: true, get: (r) => r.cpi}],
      rows, {sort: "ms", onPick: (name) => { state.w = WL.indexOf(w); openFunction(name, m.timeBase.kind); }});
  };
  el("cmpNameA").textContent = "A: " + VLONG[state.ca]; el("cmpNameB").textContent = "B: " + VLONG[state.cb];
  fnTable("cmpFnA", wa, ma); fnTable("cmpFnB", wb, mb);

  el("cmpOfficial").innerHTML = off ? `<h3>Službeno izmjereno ubrzanje (protokol D10)</h3><p class="small muted">Iz <code>perf/results.csv</code>: obična Release gradnja, `
    + `mjereno po protokolu D10 (docs/plan/PERF_LOG.md, odjeljak 7). Ovo su brojevi za izvještaj; profil iznad objašnjava odakle dolaze.</p><table style="max-width:560px"><tr><th>Faza</th><th class="num" title="Koliko puta je B (finalna verzija) brža od A.">S = A ÷ B</th><th>Unos</th></tr>`
    + Object.keys(OFFICIAL_STAGE).filter((k) => off[k]).map((k) => `<tr><td>${esc(OFFICIAL_STAGE[k])}</td><td class="num">${fmt(off[k].S, 2)}x</td><td class="small muted">${esc(off[k].id)}</td></tr>`).join("")
    + "</table>" : "";
}

// ---------- All sessions, about ----------
function renderAll() {
  let stacks = "";
  for (const s of S) stacks += `<tr><td style="white-space:nowrap" title="${esc(s.id)}">${esc(s.label)}</td><td style="width:70%">${stackHtml(s)}</td></tr>`;
  el("allStacks").innerHTML = stacks;
  el("allLegend").innerHTML = legendHtml();
  let rows = "<tr><th>Sesija</th><th>Mreža</th><th>Faza (--stage)</th><th>Prepreka</th><th>Vrsta</th><th class='num'>Trajanje profila</th><th class='num'>Uzoraka (zbir)</th><th class='num'>Ponavljanja</th><th class='num'>ms po ponavljanju</th></tr>";
  for (const s of S) {
    rows += `<tr><td>${esc(s.id)}</td><td>${s.grid}³</td><td>${esc(s.stage)}</td><td>${esc(s.obstacle)}</td><td>${esc(KIND_NAME[s.kind] || s.kind)}</td>`
      + `<td class="num">${esc(s.info["Profile Duration"] || "")}</td><td class="num">${fmt(s.total, s.kind === "time" ? 3 : 0)} ${esc(s.metrics[0] || "")}</td>`
      + `<td class="num">${esc(s.workload.ponavljanja || "–")}</td><td class="num">${esc(s.workload.ms_po_ponavljanju || "–")}</td></tr>`;
  }
  el("allTable").innerHTML = rows;
}
function renderAbout() {
  const names = new Set(), formulas = {}, events = new Map();
  for (const s of S) { for (const m of s.metrics) names.add(m); Object.assign(formulas, s.derived || {}); for (const e of s.events) events.set(e.name, e); }
  let out = "<ul class='small'>";
  for (const m of names) out += `<li><code>${esc(m)}</code>: ${helpOf(m) || "kolona iz uProf izvještaja."}${formulas[m] ? ` <span class="muted">Formula: <code>${esc(formulas[m])}</code></span>` : ""}</li>`;
  out += `<li><b>udio %</b>: dio svih uzoraka sesije koji pripada toj funkciji ili instrukciji.</li><li>${METRIC_HELP.PTI}</li></ul>`;
  if (events.size) {
    out += "<details><summary>Praćeni događaji (kako ih opisuje uProf)</summary><ul class='small'>";
    for (const e of events.values()) out += `<li><code>${esc(e.name)}</code> (razmak ${esc(e.interval)}): ${esc(e.description)}</li>`;
    out += "</ul></details>";
  }
  el("metricHelp").innerHTML = out;
  el("hardware").innerHTML = DATA.infos.map((info, i) => `<details ${i === 0 ? "open" : ""}><summary>${esc(info.group)}</summary><pre>${esc(info.text || "session_info.txt nije nađen")}</pre></details>`).join("");
}

// ---------- shell ----------
const RENDER = {grade: renderGrade, summary: renderSummary, hot: renderHot, uarch: renderUarch, cache: renderCache, asm: renderAsm, cmp: renderCompare, all: renderAll, about: renderAbout};
function render() {
  if (state.tab === "grade") snapGrade();
  el("wsel").value = state.w;
  const w = W();
  el("kinds").innerHTML = ["time", "pmc", "fe"].map((k) => `<span class="chip${ses(w, k) ? "" : " off"}" title="${ses(w, k) ? esc(ses(w, k).id) : "nije snimljeno"}">${esc(KIND_NAME[k])}</span>`).join("");
  el("tabs").innerHTML = TABS.map(([id, label]) => `<button data-tab="${id}" class="${id === state.tab ? "on" : ""}">${label}</button>`).join("");
  el("tabs").querySelectorAll("button").forEach((b) => b.onclick = () => { state.tab = b.dataset.tab; render(); window.scrollTo(0, 0); });
  for (const [id] of TABS) el("t-" + id).classList.toggle("on", id === state.tab);
  RENDER[state.tab]();
  writeHash();
}
function init() {
  el("wsel").innerHTML = WL.map((w, i) => `<option value="${i}">${esc(w.label)}</option>`).join("");
  // Start on the final version at 64^3 when the version runs are there; the hash (a shared link) wins over this.
  const start = findV("m4", 64) || VW[0];
  if (start) state.w = WL.indexOf(start);
  readHash();
  const bind = (id, key, numeric, event) => { el(id)[event || "onchange"] = () => { state[key] = numeric ? +el(id).value : el(id).value; render(); }; };
  bind("wsel", "w", true); bind("hotBasis", "basis"); bind("hotGroup", "group");
  bind("asmFn", "fn"); bind("asmKind", "asmKind"); bind("asmOrder", "asmOrder"); bind("asmHeat", "heat");
  bind("cmpA", "ca"); bind("cmpB", "cb");
  bind("uarchMin", "min", true); bind("cacheMin", "min", true);
  el("hotFilter").oninput = renderHot;
  render();
}
if (WL.length) init(); else document.querySelector("main").insertAdjacentHTML("beforeend", "<p>Nema sesija.</p>");
</script>
</body>
</html>
"""


def main():
    parser = argparse.ArgumentParser(description="AMD uProf CSV reports -> one self-contained HTML page.")
    parser.add_argument("folders", nargs="+", help="folders made by perf/uprof/collect.sh")
    parser.add_argument("-o", "--output", default=None, help="HTML file to write (default: <first folder>/report.html)")
    parser.add_argument("--summary", action="store_true", help="print a text summary instead of writing HTML")
    parser.add_argument("--results", default=str(Path(__file__).resolve().parent.parent / "perf" / "results.csv"),
                        help="perf/results.csv, for the speedups measured by protocol D10 (default: the repository's file)")
    args = parser.parse_args()

    sessions, infos = load_all(args.folders)
    if not sessions:
        print("no sessions with report.csv found", file=sys.stderr)
        return 1
    if args.summary:
        print_summary(sessions)
        return 0

    from datetime import datetime
    for session in sessions:
        for function in session["functions"]:
            del function["classes"]     # the page computes them from the instructions
            # Most counter cells are zero: write them as 0, not 0.0 (the page is a few megabytes).
            for item in function["ins"]:
                item["v"] = [int(value) if value is not None and value == int(value) else value for value in item["v"]]
    payload = {"sessions": sessions, "infos": infos, "workloads": build_workloads(sessions), "classes": CLASS_NAMES,
               "widths": WIDTH_NAMES, "official": load_official(args.results)}
    data = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    page = PAGE.replace("__GENERATED__", html.escape(datetime.now().strftime("%Y-%m-%d %H:%M"))).replace("__DATA__", data)
    output = Path(args.output) if args.output else Path(args.folders[0]) / "report.html"
    output.write_text(page, encoding="utf-8")
    print(f"written: {output} ({output.stat().st_size // 1024} KiB, {len(sessions)} sessions,"
          f" {len(payload['workloads'])} workloads)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
