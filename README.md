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

## Pokretanje demonstracije (macOS)

U macOS Terminalu koristi Unix generator, `Release` konfiguraciju i putanje sa `/`. Mac programi
nemaju ekstenziju `.exe`:

```
cmake -S Solver -B Solver/build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build Solver/build --parallel
rm -f Solver/build/smoke_demo_results.txt
./Solver/build/smoke_demo > Solver/build/smoke_demo_results.txt && open -a TextEdit Solver/build/smoke_demo_results.txt
```

`smoke_demo` poredi serijsku simulaciju prije i poslije optimizacije i ispisuje vremena,
ubrzanje i provjeru rezultata. Komanda sačuva izvještaj u `Solver/build/smoke_demo_results.txt`
i otvori ga u TextEdit prozoru. Zadani brzi mod koristi mrežu 64³; `--puno` pokreće veće mreže
i traje nekoliko minuta. Za testove tačnosti pokreni i `./Solver/build/smoke_tests`.

## Status

Serijski Solver je implementiran i može se pokretati iz komandne linije. Vizuelni prikaz kroz
Renderer još zahtijeva Integration milestone (vidi [`docs/plan/PLAN.md`](docs/plan/PLAN.md)).
