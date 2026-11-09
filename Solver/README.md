# Solver

Naša simulacija dima. **Samostalan projekat**: ne zavisi od Renderera, OpenGL-a, GLFW-a ni GLM-a, i (zasad)
ne skida nikakve biblioteke, pa se gradi i na računaru bez grafičke kartice i bez interneta.

Pravila i razlozi: [`../docs/plan/ARCHITECTURE.md`](../docs/plan/ARCHITECTURE.md).

## Gradnja i pokretanje

Windows (MSYS2 MinGW, vidi uputstvo u `../docs/`):
```
cmake -S Solver -B Solver/build -G "MinGW Makefiles"
cmake --build Solver/build --parallel
```
(Pokreće se iz korijena repozitorija. Gradnja je po zadanom **Release**.)

Linux / macOS: isto, bez `-G "MinGW Makefiles"`.

| Program | Čemu služi |
| :--- | :--- |
| `smoke_cli` | konzolni program: pokreće simulaciju i ispisuje vrijeme po fazi (`../docs/plan/CLI_OUTPUT.md`) |
| `smoke_tests` | provjere tačnosti (`../docs/plan/VERIFICATION.md`) |
| `smoke_bench` | CSV benchmark, referentni rezultat i kalibracija računara |

Primjeri: `smoke_cli --grid 128 --steps 200`, `smoke_cli --nx 64 --ny 128 --nz 64 --quiet`.
Benchmark: `Solver/build/smoke_bench --steps 100 --warmup 10 --output perf.csv`;
kalibracija: `Solver/build/smoke_bench --calibrate`;
referenca: `Solver/build/smoke_bench --reference Solver/reference/serial_64_100steps_<racunar>.bin`;
poređenje s referencom: `Solver/build/smoke_bench --verify Solver/reference/serial_64_100steps_pc2.bin`
(referenca zavisi od računara i kompajlera, vidi `../docs/plan/VERIFICATION.md`, odjeljak 3).
B0 bez vektorizacije gradi se s `-DSMOKE_DISABLE_VECTORIZATION=ON`; zadani B1 koristi optimizacije
kompajlera.
Testovi: `ctest --test-dir Solver/build --output-on-failure` ili direktno `smoke_tests`.
Ako Windows blokira `smoke_tests.exe` ("Application Control policy"), iste testove pokreće `smoke_cli --selftest`.

## Šta je u folderima

```
include/smoke/   javni headeri; SmokeSolver.h je jedina klasa koju drugi koriste
src/             implementacija; src/stages/ ima po jednu datoteku za svaku fazu koraka
apps/smoke_cli/  konzolni program
tests/           testovi (doctest, odluka D5); pokretanje: smoke_tests ili smoke_cli --selftest
third_party/     doctest.h (zapakovan u repozitorij, bez preuzimanja pri gradnji)
bench/           mjerenje
reference/       referentni rezultati (kasnije)
```

## Stanje (M3)

Radi cijeli korak s opcionim vorticity confinementom; dijagnostika i mjerenje po fazi (uključujući
Jacobi iteraciju); `smoke_bench` s CSV, kalibracijom i serijskom referencom; testovi T1 do T10.
Zadani confinement `ε = 5` vizualno je podešen na 64³; `smoke_cli --vorticity-eps 0` ga isključuje.
