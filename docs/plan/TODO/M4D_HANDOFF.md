# M4D_HANDOFF: privremena bilješka za nastavak poslije restarta (2026-11-11)

Privremeni fajl. Kad se P1 završi s brojačima i prođe tačka G0, obrisati ga (sadržaj tada živi u PROFILE.md).

## Kako nastaviti

U novoj sesiji reci Claudeu: **"Pročitaj docs/plan/TODO/M4D_HANDOFF.md i nastavi gdje si stao."**

## Gdje smo stali

- Grana: `feature/m4d-profiling` (polazi od `docs/execution-plan`). Ništa nije pushano.
- Zadnji commit: `7570a8c`. Radno stablo čisto, osim `Renderer/imgui.ini` i `docs/Sazetak_Simulacija_Dima.pdf`
  (nisu dio plana, ne dirati) i ovog fajla.
- Stojimo na tački **G0** (poslije P1) i **G1** (poslije P2). Obje čekaju Erminovu odluku.

| Faza | Stanje | Commit |
| :--- | :--- | :--- |
| Dokumenti plana (`docs/execution-plan`) | gotovo | `c9dca43` |
| P0: kalibracija memorije, `--hwinfo`, README/PLAN | gotovo | `7a80ec8` |
| P1.2, P1.3: profilna gradnja, `smoke_prof` | gotovo | `f495144` |
| P2: `smoke_bench --convergence`, tabela u MATH.md | izmjereno, odluka D15 nije upisana (G1) | `358e2ac` |
| P1.4 do P1.6: skupljanje, HTML, PROFILE.md | gotovo **samo s vremenskim uzorkovanjem (TBP)** | `c9732d4`, `7570a8c` |
| P1 s hardverskim brojačima | **čeka isključen WSL2 / Hyper-V i restart** | |

Provjera stanja: 48/48 testova u običnoj gradnji, gradnji bez AVX2 i profilnoj gradnji; `smoke_bench --verify`
bit po bit isto kao `serial_64_100steps_pc2.bin`.

## Zašto restart

`AMDuProfCLI collect --config assess_ext` vraća *"Only timer-based profiling (TBP) is available when WSL2 is
enabled."* Dok Windows radi pod Hyper-V hipervizorom, hardverski brojači (keš promašaji, pogrešna grananja, IPC)
i IBS nisu dostupni. Ermin isključuje WSL2 / hipervizor i restartuje računar.

## Prvi koraci poslije restarta

1. Provjeriti da je hipervizor ugašen:
   `& "C:\Program Files\AMD\AMDuProf\bin\AMDuProfCLI.exe" info --system`
   Očekuje se `Hypervisor Enabled : No`; pogledati i redove `IBS` i `EBP Supported`.
   (Ne zvati `--help`: čeka pritisak tipke. Tekst pomoći je u `C:\Program Files\AMD\AMDuProf\bin\Help\text\`.)
2. Ako je Ermin mijenjao i druge postavke (npr. XMP za RAM na 3200 MT/s): ponovo izmjeriti
   `Solver\build\smoke_bench.exe --calibrate` i `Get-CimInstance Win32_PhysicalMemory | Select Speed,ConfiguredClockSpeed`,
   pa ispraviti tabelu hardvera i teorijski maksimum u PROFILE.md (odjeljci 1 i 2) i PERF_LOG.md (odjeljak 8).
   Sadašnje vrijednosti: 2400 MT/s, teorijski 38.4 GB/s, triad 17.9 GB/s, E za Jacobi na 128³ oko 0.94.
   **Pazi:** promjena brzine RAM-a mijenja vremena, pa stare PC-2 osnove (PERF_LOG odjeljci 6 i 7) više nisu
   uporedive s novim mjerenjima; S i Δ se ionako uvijek mjere u istoj sesiji.
3. Probati jedno kratko skupljanje s brojačima (iz MSYS2 / Git Bash, iz korijena repozitorija):
   `"/c/Program Files/AMD/AMDuProf/bin/AMDuProfCLI.exe" collect --config assess_ext --start-delay 2 -o <folder> Solver/build-prof/smoke_prof.exe --grid 64 --seconds 8 --stage jacobi`
   pa `report -i <folder> --cutoff 0 --report-output <folder>\r.csv` i **pogledati stvarni CSV** (nazive kolona,
   jedinice) prije mijenjanja konvertera. `tools/uprof_report.py` čita kolone metrika po imenu, ali format s
   brojačima još nije viđen.
4. Puno skupljanje: `perf/uprof/collect.sh ebp assess_ext` (po potrebi i `data_access`, `branch`, `inst_access`;
   IBS ako ga `info --system` prikaže). Dostupne konfiguracije: `AMDuProfCLI info --list collect-configs`.
5. HTML s obje grupe sesija:
   `python tools/uprof_report.py perf/uprof/2026-11-11_tbp perf/uprof/<datum>_ebp -o perf/uprof/report.html`
6. Dopuniti PROFILE.md: odjeljak 3.3 (šta sada radi), tabelu hipoteza 4.3 (H1, H2, H4, H5 s brojačima) i kandidate
   u odjeljku 5. Ne izmišljati pragove (`TODO(measure)`).
7. Commit na `feature/m4d-profiling`, pa opet stati na G0.

Ako Smart App Control blokira novu gradnju nekog `.exe` ("Application Control policy has blocked this file"):
ne zaobilaziti, javiti Erminu. Jednom se desilo sa `smoke_bench.exe`; sljedeća gradnja je prošla.

## Nalazi do sada (TBP, `perf/uprof/2026-11-11_tbp/report.html`)

Udio u cijelom koraku:

| Faza | 64³ | 128³ |
| :--- | ---: | ---: |
| Jacobi | 35.2% | 49.4% |
| Advekcija | 43.4% | 27.0% |
| Sile | 8.7% | 9.8% |
| `setBoundary` | 7.5% | 8.5% |

- **H1 (Jacobi na 128³ ograničen memorijom): podržana posredno.** Vrijeme po ćeliji 2.03 puta veće nego na 64³,
  E oko 0.94. Keš promašaji nisu izmjereni.
- **H2 (Jacobi na 64³ ograničen dijeljenjem): otvorena.** 53% uzoraka na upisu odmah iza `vdivps`.
- **H3 (advekcija i oponašani gather): djelimično.** Oko 36% u slaganju vektora i pojedinačnim čitanjima, oko 41%
  u računu težina i interpolacije; advekcija nije ograničena memorijom.
- **H4, H5 (grananja, frontend): nisu izmjerene**, trebaju brojači.
- **Izvan hipoteza:** `setBoundary` je 7.5 do 8.5% koraka (poziva se poslije svake Jacobi iteracije; skalarna
  petlja koja množi predznakom, za pritisak je to množenje jedinicom).

## Odluke koje čekaju Ermina

**G0: šta optimizirati** (ništa nije implementirano). Kandidati po jačini dokaza:
1. `setBoundary` bez množenja za skalarna polja: najjači dokaz, može ostati bit po bit isto. Claudeova preporuka kao prvo.
2. Jacobi na 128³, manje prometa memorije: samo za velike mreže, dobitak nije izmjeren.
3. Množenje s 1/6 umjesto `vdivps`: mijenja rezultat, D11 to trenutno isključuje.
4. Pravi gather u advekciji: dobitak nije izmjeren.

**G1: odluka D15** (MATH.md, odjeljak 5). 128³, isti K u svih 100 koraka, `max|div|` poslije / rms:

| K | ε = 5 | ε = 0 |
| ---: | :--- | :--- |
| 10 | 3.49 / 0.0451 | 0.965 / 0.0366 |
| 40 | 2.84 / 0.0317 | 0.293 / 0.0122 |
| 1000 | 2.47 / 0.0297 | 0.164 / 0.0016 |

Claudeova preporuka: Jacobi ostaje (ε = 5 je konačan po D9 i tu je K = 40 blizu poda); red-black eksperiment ima
smisla samo ako je važan slučaj ε = 0.

## Poslije G0 i G1

Po EXECUTION_PLAN.md: P3 (M-VIZ) na grani `feature/mviz`. Prva verzija je jednostavna: dim u kutiji (D17).
Python 3.15.2 je instaliran (`python`). Sirovi uProf fajlovi su na disku u `perf/uprof` (oko 129 MB), nisu u gitu.
