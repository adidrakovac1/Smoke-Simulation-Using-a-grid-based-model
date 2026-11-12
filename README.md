# Smoke-Simulation-Using-a-grid-based-model

3D simulacija dima na mreži ćelija (Eulerov pristup) u C++-u, projekat za predmet
**Paralelni računarski sistemi** (ETF Sarajevo). Cilj: serijska verzija, pa ubrzanje (vektorizacija,
OpenMP, eventualno GPU) uz mjerenje šta je koja tehnika donijela, i na kraju prikaz dima u OpenGL rendereru.

**Tim:** Ermin Hadžić, Ajdin Đinalić, Adi Drakovac.

## Struktura

| Folder | Šta je |
| :--- | :--- |
| `Solver/` | **Naš kod**: simulacija dima. Samostalan CMake projekat, bez OpenGL-a, gradi se i pokreće bez renderera. |
| `Renderer/` | OpenGL forward renderer prethodnog tima (tuđi kod, ne diramo ga osim navedenih mjesta). |
| `Integration/` | Spoj Solvera i Renderera. Tek u zadnjem milestone-u (vizualizacija). |
| `docs/` | Literatura (PDF), uputstvo za Windows i planovi u `docs/plan/`. |
| `perf/` | Evidencija mjerenja (`results.csv`). |

Programi u `Solver/build`: `smoke_cli` (simulacija u konzoli), `smoke_tests` (62 testa), `smoke_bench`
(mjerenje, `--verify`, `--calibrate`, `--hwinfo`), `smoke_lab` i `smoke_demo` (poređenje verzija); opis je u
[`Solver/README.md`](Solver/README.md).

Početi čitanjem: [`docs/plan/README.md`](docs/plan/README.md).

## Brzi početak (Windows, MinGW)

Instalacija alata i pokretanje renderera: [`docs/Windows_Setup_Uputstvo.pdf`](docs/Windows_Setup_Uputstvo.pdf).

Solver (ne treba renderer ni internet):

```
cmake -S Solver -B Solver/build -G "MinGW Makefiles"
cmake --build Solver/build --parallel
Solver\build\smoke_tests.exe
Solver\build\smoke_cli.exe --grid 64
```

## Status

Solver je implementiran i serijski optimiziran (M0 do M4; PC-2: cijeli korak 2.2x do 3.2x brži od osnove bez
vektorizacije B0, bit po bit isti rezultat; [`docs/plan/PERF_LOG.md`](docs/plan/PERF_LOG.md), odjeljak 7).
U toku je M4d: kalibracija memorije je ispravljena i postoji tabela hardvera
([`docs/plan/PROFILE.md`](docs/plan/PROFILE.md)). Slijedi (vidi [`docs/plan/EXECUTION_PLAN.md`](docs/plan/EXECUTION_PLAN.md)):
M4d profiliranje (AMD uProf), M-VIZ vizualizacija u rendereru, M-OBS prepreke i mesh (`.obj`).
Paralelizacija (OpenMP, GPU) nije u planu dok se ne odluči (odluka D8).
