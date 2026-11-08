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

Skelet postoji i gradi se. Faze simulacije još nisu implementirane (vidi [`docs/plan/PLAN.md`](docs/plan/PLAN.md)).
