#!/usr/bin/env python3
"""Turns AMD uProf CSV reports into one self-contained HTML page (docs/plan/EXECUTION_PLAN.md, P1.5).

Input: a folder made by perf/uprof/collect.sh. Each sub-folder (g64_jacobi, g128_all, ...) is one session with
    report.csv    function table      (AMDuProfCLI report --cutoff 0)
    detail.csv    samples per instruction (AMDuProfCLI report --detail --disasm-only --show-percentage)
    workload.txt  what smoke_prof printed
and the folder itself has session_info.txt (hardware, profiler version).

Output: one HTML file with all CSS, JavaScript and data inside (no network access needed).

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
    ("Advekcija", r"advect"),
    ("Sile", r"vorticity|confinement|Buoyancy|applyForces"),
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
    """Per-instruction sample shares from detail.csv: {full function name: [instruction, ...]}."""
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
                                "values": [number(cell) for cell in row[5:5 + len(extra)]]})
    return result, extra


def parse_workload(path):
    workload = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if ":" in line and not line.startswith("smoke_prof"):
                key, value = line.split(":", 1)
                workload[key.strip()] = value.strip()
    return workload


def load_session(folder, group):
    info, events, metrics, functions, derived = parse_report(folder / "report.csv")
    detail, detail_metrics = parse_detail(folder / "detail.csv")
    match = re.match(r"g(\d+)_(\w+)$", folder.name)
    grid, stage = (int(match.group(1)), match.group(2)) if match else (0, folder.name)
    total = sum(f["values"][0] or 0.0 for f in functions) if metrics else 0.0
    for function in functions:
        function["share"] = 100.0 * (function["values"][0] or 0.0) / total if total else 0.0
        instructions = sorted(detail.get(function["full"], []), key=lambda item: -item["share"])
        function["instructions"] = instructions[:40]
        classes = {}
        for item in instructions:
            classes[item["cls"]] = classes.get(item["cls"], 0.0) + item["share"]
        function["classes"] = sorted(classes.items(), key=lambda pair: -pair[1])
        del function["full"]
    return {"id": f"{group}/{folder.name}", "group": group, "grid": grid, "stage": stage,
            "label": f"{grid}^3 {stage} ({group})", "info": info, "events": events, "metrics": metrics,
            "derived": derived, "detailMetrics": detail_metrics,
            "total": total, "functions": functions, "workload": parse_workload(folder / "workload.txt")}


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
            for item in function["instructions"][:6]:
                print(f"          {item['share']:6.2f}%  {item['text']}")


PAGE = r"""<!doctype html>
<html lang="bs">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Profil simulacije dima (AMD uProf)</title>
<style>
:root { --bg:#f6f7f9; --card:#ffffff; --text:#1b1f24; --muted:#5b6672; --line:#d9dee5; --accent:#2f6fb7;
        --bar:#4c8fd6; --bar2:#e0913a; --good:#2e8b57; --bad:#c2453c; --code:#eef1f5; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#14171b; --card:#1d2127; --text:#e6e9ed; --muted:#9aa5b1; --line:#333a44; --accent:#7fb2ea;
          --bar:#4c8fd6; --bar2:#e0913a; --good:#5fc08a; --bad:#ef7d74; --code:#262b33; }
}
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--text); font:15px/1.5 system-ui, "Segoe UI", sans-serif; }
main { max-width:1180px; margin:0 auto; padding:20px 16px 60px; }
h1 { font-size:24px; margin:8px 0 4px; } h2 { font-size:18px; margin:0 0 10px; } h3 { font-size:15px; margin:14px 0 6px; }
p { margin:6px 0; } .muted { color:var(--muted); } .small { font-size:13px; }
section { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:16px 18px; margin:16px 0; }
table { border-collapse:collapse; width:100%; font-size:13.5px; }
th, td { padding:5px 8px; border-bottom:1px solid var(--line); text-align:left; vertical-align:middle; }
th { cursor:pointer; user-select:none; white-space:nowrap; position:sticky; top:0; background:var(--card); }
th.sorted::after { content:" \25BC"; font-size:10px; } th.sorted.asc::after { content:" \25B2"; }
td.num, th.num { text-align:right; font-variant-numeric:tabular-nums; }
.scroll { overflow-x:auto; max-height:520px; overflow-y:auto; }
pre, code { font-family:Consolas, "Cascadia Mono", monospace; font-size:12.5px; }
pre { background:var(--code); padding:10px 12px; border-radius:8px; overflow-x:auto; margin:6px 0; }
select, input { font:inherit; padding:5px 8px; border:1px solid var(--line); border-radius:6px; background:var(--card); color:var(--text); }
.row { display:flex; gap:12px; flex-wrap:wrap; align-items:center; margin:8px 0; }
.bar { height:14px; background:var(--bar); border-radius:3px; min-width:1px; }
.barrow { display:grid; grid-template-columns:minmax(120px, 260px) 1fr 64px; gap:8px; align-items:center; margin:3px 0; font-size:13px; }
.barrow .name { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.barrow .val { text-align:right; font-variant-numeric:tabular-nums; }
.stack { display:flex; height:22px; border-radius:5px; overflow:hidden; border:1px solid var(--line); }
.stack div { height:100%; } .legend { display:flex; flex-wrap:wrap; gap:4px 14px; font-size:13px; margin-top:6px; }
.dot { display:inline-block; width:10px; height:10px; border-radius:2px; margin-right:5px; }
.pos { color:var(--bad); } .neg { color:var(--good); }
.tag { display:inline-block; padding:1px 7px; border-radius:10px; font-size:12px; border:1px solid var(--line); white-space:nowrap; }
.note { border-left:3px solid var(--accent); padding:4px 12px; margin:10px 0; }
details summary { cursor:pointer; color:var(--accent); }
tr.pick { cursor:pointer; } tr.pick:hover td { background:var(--code); } tr.active td { background:var(--code); font-weight:600; }
</style>
</head>
<body>
<main>
<h1>Profil simulacije dima</h1>
<p class="muted">AMD uProf, program <code>smoke_prof</code>, serijska verzija (jedna nit). Generisano: __GENERATED__.
Ovaj fajl je samostalan: nema vanjskih skripti ni stilova.</p>

<section>
<h2>Kako čitati ovaj izvještaj</h2>
<p><b>Šta je mjereno.</b> Profiler koji uzorkuje prekida program u pravilnim razmacima i zapiše koja se instrukcija
upravo izvršavala. Funkcija koja dobije više uzoraka troši više vremena.</p>
<div id="metricHelp"></div>
<p class="note small"><b>Oprez kod instrukcija ("skid").</b> Uzorak se često pripiše instrukciji <i>poslije</i> one koja je stvarno
spora (procesor prekid obradi tek kad spora instrukcija završi). Zato visok postotak na jednoj instrukciji znači
"ovdje ili neposredno prije". Vremensko uzorkovanje ne kaže <i>zašto</i> je mjesto sporo (keš promašaj, dijeljenje,
zavisnost); za to trebaju hardverski brojači.</p>
<p class="small muted">Profilna gradnja ima <code>-g -fno-omit-frame-pointer</code>; služi za nalaženje mjesta. Ubrzanja (S, Δ) se mjere
na običnoj Release gradnji (docs/plan/PERF_LOG.md).</p>
</section>

<section>
<h2>Računar i sesije</h2>
<div id="hardware"></div>
<h3>Sesije</h3>
<div class="scroll"><table id="sessionTable"></table></div>
</section>

<section>
<h2>Pregled: udio faza u svakoj sesiji</h2>
<p class="small muted">Svaka traka je 100% uzoraka te sesije, podijeljeno po fazama koraka (funkcije su fazama pridružene po imenu).</p>
<div id="overview"></div>
<div class="legend" id="phaseLegend"></div>
</section>

<section>
<h2>Funkcije u jednoj sesiji</h2>
<div class="row">
  <label>Sesija <select id="sessionSelect"></select></label>
  <label>Faza <select id="phaseFilter"></select></label>
  <label>Filter imena <input id="nameFilter" placeholder="npr. advect" size="16"></label>
  <span class="muted small" id="sessionNote"></span>
</div>
<h3>Najvrelije funkcije</h3>
<div id="hotBars"></div>
<h3>Tabela (klik na zaglavlje sortira, klik na red pokazuje instrukcije)</h3>
<div class="scroll"><table id="functionTable"></table></div>
</section>

<section>
<h2>Instrukcije u izabranoj funkciji</h2>
<p class="muted small" id="instructionTitle">Klikni red u tabeli funkcija.</p>
<div id="classBars"></div>
<div class="scroll"><table id="instructionTable"></table></div>
</section>

<section>
<h2>Poređenje dvije sesije</h2>
<p class="small muted">Npr. 64³ naspram 128³, ili prije i poslije izmjene. Delta je razlika udjela (B − A) u procentnim poenima;
apsolutne vrijednosti različitih sesija nisu direktno uporedive ako su trajale različito.</p>
<div class="row">
  <label>A <select id="compareA"></select></label>
  <label>B <select id="compareB"></select></label>
</div>
<div id="compareWorkload" class="small"></div>
<h3>Po fazama</h3>
<div class="scroll"><table id="comparePhases"></table></div>
<h3>Po funkcijama</h3>
<div class="scroll"><table id="compareTable"></table></div>
</section>
</main>

<script id="data" type="application/json">__DATA__</script>
<script>
"use strict";
const DATA = JSON.parse(document.getElementById("data").textContent);
const sessions = DATA.sessions;
const PHASE_COLORS = {"Jacobi":"#4c8fd6","Projekcija (ostalo)":"#8fbbe8","Advekcija":"#e0913a","Sile":"#5aa469",
  "Rubni uslovi":"#b07cc6","Izvor":"#d4b63a","Kopiranje i punjenje":"#8a94a0","Ostalo":"#c2453c"};
const PHASES = Object.keys(PHASE_COLORS);
const METRIC_HELP = {
  "CPU_TIME": "Vrijeme procesora u sekundama: broj uzoraka puta razmak uzorkovanja (1 ms). Funkcija sa 2.0 s je bila zatečena u izvršavanju 2000 puta.",
  "IPC": "Instrukcija po ciklusu. Veće je bolje; ispod 1 obično znači čekanje (memorija, dijeljenje, pogrešna grananja).",
  "CPI": "Ciklusa po instrukciji (1 / IPC). Manje je bolje.",
  "PTI": "\"pti\" = na 1000 izvršenih instrukcija (per thousand instructions). Npr. 12 promašaja pti = 12 promašaja na svakih 1000 instrukcija."
};
// Columns of the event-based configs (assess_ext), by exact name. "Demand" fills are the ones a load or store
// itself had to wait for; lines brought in ahead of time by the hardware prefetcher are not counted there.
const METRIC_EXACT = {
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

function phaseShares(s) {
  const out = {};
  for (const f of s.functions) out[f.phase] = (out[f.phase] || 0) + f.share;
  return out;
}
function tag(phase) { return `<span class="tag"><span class="dot" style="background:${PHASE_COLORS[phase] || "#999"}"></span>${esc(phase)}</span>`; }

function renderHelp() {
  const names = new Set();
  for (const s of sessions) for (const m of s.metrics) names.add(m);
  let htmlText = "<p><b>Mjerne jedinice u ovom izvještaju.</b></p><ul class='small'>";
  const formulas = {};
  for (const s of sessions) Object.assign(formulas, s.derived || {});
  for (const m of names) {
    const key = Object.keys(METRIC_HELP).find((k) => m.toUpperCase().includes(k));
    const text = METRIC_EXACT[m] || (key ? METRIC_HELP[key] : "kolona iz uProf izvještaja; značenje je u opisu događaja ispod (ako ga uProf navodi).");
    htmlText += `<li><code>${esc(m)}</code>: ${text}${formulas[m] ? ` <span class="muted">Formula: <code>${esc(formulas[m])}</code></span>` : ""}</li>`;
  }
  htmlText += `<li><b>udio %</b>: dio svih uzoraka sesije koji pripada toj funkciji ili instrukciji.</li>`;
  htmlText += `<li>${METRIC_HELP.PTI} (Pojavljuje se samo u sesijama s hardverskim brojačima.)</li></ul>`;
  const events = new Map();
  for (const s of sessions) for (const e of s.events) events.set(e.name, e);
  if (events.size) {
    htmlText += "<p class='small'><b>Praćeni događaji:</b></p><ul class='small'>";
    for (const e of events.values()) htmlText += `<li><code>${esc(e.name)}</code> (razmak ${esc(e.interval)}): ${esc(e.description)}</li>`;
    htmlText += "</ul>";
  }
  el("metricHelp").innerHTML = htmlText;
}

function renderHardware() {
  let out = "";
  for (const info of DATA.infos) {
    out += `<details ${DATA.infos.length === 1 ? "open" : ""}><summary>${esc(info.group)}: hardver, gradnja, profiler</summary><pre>${esc(info.text || "session_info.txt nije nađen")}</pre></details>`;
  }
  el("hardware").innerHTML = out;
  let rows = "<tr><th>Sesija</th><th>Mreža</th><th>Faza (--stage)</th><th>Tip profila</th><th class='num'>Trajanje profila</th><th class='num'>Uzoraka (zbir)</th><th class='num'>Ponavljanja</th><th class='num'>ms po ponavljanju</th></tr>";
  for (const s of sessions) {
    rows += `<tr><td>${esc(s.id)}</td><td>${s.grid}³</td><td>${esc(s.stage)}</td><td>${esc(s.info["Profile Session Type"] || "")}</td>`
      + `<td class="num">${esc(s.info["Profile Duration"] || "")}</td><td class="num">${fmt(s.total, 3)} ${esc(s.metrics[0] || "")}</td>`
      + `<td class="num">${esc(s.workload.ponavljanja || "–")}</td><td class="num">${esc(s.workload.ms_po_ponavljanju || "–")}</td></tr>`;
  }
  el("sessionTable").innerHTML = rows;
}

function renderOverview() {
  let out = "";
  for (const s of sessions) {
    const shares = phaseShares(s);
    let stack = "";
    for (const p of PHASES) if (shares[p] > 0.05) stack += `<div style="width:${shares[p]}%;background:${PHASE_COLORS[p]}" title="${esc(p)}: ${fmt(shares[p], 1)}%"></div>`;
    const top = PHASES.filter((p) => shares[p] >= 5).map((p) => `${p} ${fmt(shares[p], 0)}%`).join(", ");
    out += `<div class="barrow" style="grid-template-columns:minmax(120px,200px) 1fr"><div class="name" title="${esc(s.id)}">${esc(s.label)}</div><div><div class="stack">${stack}</div><div class="small muted">${esc(top)}</div></div></div>`;
  }
  el("overview").innerHTML = out;
  el("phaseLegend").innerHTML = PHASES.map((p) => `<span><span class="dot" style="background:${PHASE_COLORS[p]}"></span>${esc(p)}</span>`).join("");
}

let sortKey = "share", sortAsc = false, activeFunction = null;
function currentSession() { return sessions[+el("sessionSelect").value]; }

function renderFunctions() {
  const s = currentSession();
  const phase = el("phaseFilter").value, text = el("nameFilter").value.toLowerCase();
  let list = s.functions.filter((f) => (!phase || f.phase === phase) && (!text || f.name.toLowerCase().includes(text)));
  const value = (f) => sortKey === "name" ? f.name.toLowerCase() : sortKey === "phase" ? f.phase : sortKey === "share" ? f.share : f.values[+sortKey];
  list = list.slice().sort((a, b) => { const x = value(a), y = value(b); return (x < y ? -1 : x > y ? 1 : 0) * (sortAsc ? 1 : -1); });
  el("sessionNote").textContent = `${list.length} od ${s.functions.length} funkcija; zbir ${fmt(s.total, 3)} ${s.metrics[0] || ""}`;

  const hot = s.functions.slice().sort((a, b) => b.share - a.share).slice(0, 10);
  const max = hot.length ? hot[0].share : 1;
  el("hotBars").innerHTML = hot.map((f) => `<div class="barrow"><div class="name" title="${esc(f.name)}">${esc(f.name)}</div>`
    + `<div><div class="bar" style="width:${100 * f.share / max}%;background:${PHASE_COLORS[f.phase] || "#999"}"></div></div><div class="val">${fmt(f.share, 1)}%</div></div>`).join("");

  const head = (key, label, num) => `<th data-key="${key}" class="${num ? "num " : ""}${sortKey === String(key) ? "sorted" + (sortAsc ? " asc" : "") : ""}">${esc(label)}</th>`;
  let rows = "<tr>" + head("name", "Funkcija") + head("phase", "Faza") + head("share", "udio %", true)
    + s.metrics.map((m, i) => head(i, m, true)).join("") + "<th>Modul</th></tr>";
  for (const f of list) {
    rows += `<tr class="pick ${f === activeFunction ? "active" : ""}" data-name="${esc(f.name)}"><td>${esc(f.name)}</td><td>${tag(f.phase)}</td><td class="num">${fmt(f.share, 2)}</td>`
      + f.values.map((v) => `<td class="num">${fmt(v, 4)}</td>`).join("") + `<td class="muted">${esc(f.module)}</td></tr>`;
  }
  el("functionTable").innerHTML = rows;
  el("functionTable").querySelectorAll("th[data-key]").forEach((th) => th.onclick = () => {
    const key = th.dataset.key;
    if (sortKey === key) sortAsc = !sortAsc; else { sortKey = key; sortAsc = key === "name" || key === "phase"; }
    renderFunctions();
  });
  el("functionTable").querySelectorAll("tr.pick").forEach((tr) => tr.onclick = () => {
    activeFunction = s.functions.find((f) => f.name === tr.dataset.name);
    renderFunctions(); renderInstructions();
  });
}

function renderInstructions() {
  const f = activeFunction, s = currentSession();
  if (!f || !f.instructions.length) {
    el("instructionTitle").textContent = f ? `${f.name}: uProf za ovu funkciju nije dao instrukcije.` : "Klikni red u tabeli funkcija.";
    el("classBars").innerHTML = ""; el("instructionTable").innerHTML = ""; return;
  }
  el("instructionTitle").innerHTML = `<b>${esc(f.name)}</b> u sesiji ${esc(s.label)}: ${fmt(f.share, 1)}% uzoraka sesije. Postoci ispod su dio <b>svih</b> uzoraka sesije.`;
  const max = f.classes.length ? f.classes[0][1] : 1;
  el("classBars").innerHTML = "<h3>Po vrsti instrukcije</h3>" + f.classes.map(([name, v]) => `<div class="barrow"><div class="name">${esc(name)}</div>`
    + `<div><div class="bar" style="width:${100 * v / max}%;background:var(--bar2)"></div></div><div class="val">${fmt(v, 2)}%</div></div>`).join("");
  const top = f.instructions[0].share || 1;
  const extra = s.detailMetrics || [];
  let rows = "<tr><th class='num'>udio %</th><th style='min-width:120px'></th><th>Adresa</th><th>Instrukcija (AT&amp;T: odredište je zadnje)</th><th>Vrsta</th>"
    + extra.map((m) => `<th class="num">${esc(m)}</th>`).join("") + "</tr>";
  for (const i of f.instructions) rows += `<tr><td class="num">${fmt(i.share, 2)}</td><td><div class="bar" style="width:${100 * i.share / top}%"></div></td>`
    + `<td><code>${esc(i.address)}</code></td><td><code>${esc(i.text)}</code></td><td class="small">${esc(i.cls)}</td>`
    + extra.map((m, k) => `<td class="num">${fmt((i.values || [])[k], 2)}</td>`).join("") + "</tr>";
  el("instructionTable").innerHTML = "<h3>Najvrelije instrukcije (do 40)</h3>"
    + (extra.length ? "<p class='small muted'>Kolone desno su brojači za tu jednu instrukciju. Na instrukcijama s malo uzoraka su nepouzdane (mali brojevi u omjeru); gledaj ih samo na vrelim redovima.</p>" : "")
    + rows;
}

function delta(v) { const c = v > 0.05 ? "pos" : v < -0.05 ? "neg" : "muted"; return `<td class="num ${c}">${v > 0 ? "+" : ""}${fmt(v, 2)}</td>`; }
function renderCompare() {
  const a = sessions[+el("compareA").value], b = sessions[+el("compareB").value];
  const w = (s) => `${esc(s.label)}: ${esc(s.workload.ms_po_ponavljanju || "–")} ms po ponavljanju, ${esc(s.workload.ponavljanja || "–")} ponavljanja`;
  el("compareWorkload").innerHTML = `<p>A = ${w(a)}<br>B = ${w(b)}</p>`;
  const pa = phaseShares(a), pb = phaseShares(b);
  let rows = "<tr><th>Faza</th><th class='num'>A udio %</th><th class='num'>B udio %</th><th class='num'>delta (B − A)</th></tr>";
  for (const p of PHASES) if ((pa[p] || 0) + (pb[p] || 0) > 0.05) rows += `<tr><td>${tag(p)}</td><td class="num">${fmt(pa[p] || 0, 2)}</td><td class="num">${fmt(pb[p] || 0, 2)}</td>${delta((pb[p] || 0) - (pa[p] || 0))}</tr>`;
  el("comparePhases").innerHTML = rows;
  const names = new Map();
  for (const f of a.functions) names.set(f.name, {phase: f.phase, a: f, b: null});
  for (const f of b.functions) { const e = names.get(f.name) || {phase: f.phase, a: null, b: null}; e.b = f; names.set(f.name, e); }
  const list = [...names.entries()].map(([name, e]) => ({name, phase: e.phase, sa: e.a ? e.a.share : 0, sb: e.b ? e.b.share : 0,
    va: e.a ? e.a.values[0] : null, vb: e.b ? e.b.values[0] : null})).filter((r) => r.sa + r.sb >= 0.1)
    .sort((x, y) => Math.max(y.sa, y.sb) - Math.max(x.sa, x.sb));
  const metric = esc(a.metrics[0] || "");
  rows = `<tr><th>Funkcija</th><th>Faza</th><th class='num'>A udio %</th><th class='num'>B udio %</th><th class='num'>delta (B − A)</th><th class='num'>A ${metric}</th><th class='num'>B ${metric}</th></tr>`;
  for (const r of list) rows += `<tr><td>${esc(r.name)}</td><td>${tag(r.phase)}</td><td class="num">${fmt(r.sa, 2)}</td><td class="num">${fmt(r.sb, 2)}</td>${delta(r.sb - r.sa)}<td class="num">${fmt(r.va, 3)}</td><td class="num">${fmt(r.vb, 3)}</td></tr>`;
  el("compareTable").innerHTML = rows + `<tr><td colspan="7" class="muted small">Prikazane su funkcije s najmanje 0.1% u A ili B.</td></tr>`;
}

function init() {
  const options = sessions.map((s, i) => `<option value="${i}">${esc(s.label)}</option>`).join("");
  for (const id of ["sessionSelect", "compareA", "compareB"]) el(id).innerHTML = options;
  el("phaseFilter").innerHTML = `<option value="">sve</option>` + PHASES.map((p) => `<option>${esc(p)}</option>`).join("");
  const find = (grid, stage) => Math.max(0, sessions.findIndex((s) => s.grid === grid && s.stage === stage));
  el("sessionSelect").value = find(64, "all"); el("compareA").value = find(64, "jacobi"); el("compareB").value = find(128, "jacobi");
  el("sessionSelect").onchange = () => { activeFunction = currentSession().functions[0] || null; renderFunctions(); renderInstructions(); };
  el("phaseFilter").onchange = renderFunctions; el("nameFilter").oninput = renderFunctions;
  el("compareA").onchange = renderCompare; el("compareB").onchange = renderCompare;
  renderHelp(); renderHardware(); renderOverview();
  activeFunction = currentSession().functions[0] || null;
  renderFunctions(); renderInstructions(); renderCompare();
}
if (sessions.length) init(); else document.querySelector("main").insertAdjacentHTML("beforeend", "<p>Nema sesija.</p>");
</script>
</body>
</html>
"""


def main():
    parser = argparse.ArgumentParser(description="AMD uProf CSV reports -> one self-contained HTML page.")
    parser.add_argument("folders", nargs="+", help="folders made by perf/uprof/collect.sh")
    parser.add_argument("-o", "--output", default=None, help="HTML file to write (default: <first folder>/report.html)")
    parser.add_argument("--summary", action="store_true", help="print a text summary instead of writing HTML")
    args = parser.parse_args()

    sessions, infos = load_all(args.folders)
    if not sessions:
        print("no sessions with report.csv found", file=sys.stderr)
        return 1
    if args.summary:
        print_summary(sessions)
        return 0

    from datetime import datetime
    data = json.dumps({"sessions": sessions, "infos": infos}, ensure_ascii=False).replace("</", "<\\/")
    page = PAGE.replace("__GENERATED__", html.escape(datetime.now().strftime("%Y-%m-%d %H:%M"))).replace("__DATA__", data)
    output = Path(args.output) if args.output else Path(args.folders[0]) / "report.html"
    output.write_text(page, encoding="utf-8")
    print(f"written: {output} ({output.stat().st_size // 1024} KiB, {len(sessions)} sessions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
