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
| `smoke_prof` | ustaljeno opterećenje za profiler (AMD uProf): `--grid N --seconds S --stage all\|jacobi\|advect\|forces`; profilna gradnja je `-DSMOKE_PROFILE_BUILD=ON` u `Solver/build-prof` (`../docs/plan/PROFILE.md`, odjeljak 3) |
| `smoke_lab` | poređenje verzija jednog jezgra jednu do druge (M3 bez vektorizacije, ručno odmotavanje, AoS/SoA, auto, restrict, AVX2, fuzija), s vremenom, ubrzanjem i provjerom tačnosti, kao primjer iz laba; za demonstraciju |
| `smoke_demo` | **program za prezentaciju**: cijela simulacija u tri verzije (Neoptimizirana M3, M3 + auto-vektorizacija, Optimizirana M4), ukupno vrijeme, ms po koraku, koraka u sekundi, vrijeme po fazi, ubrzanje S i provjera tačnosti; zatim tablice `smoke_lab` i spisak izmijenjenih funkcija i tehnika |

`smoke_demo` bez argumenata (mreža 64³, 100 koraka, 3 runde) traje oko pola minute; `smoke_demo --puno` radi
mreže 32³ do 128³ (nekoliko minuta). Ostale opcije: `--grid N`, `--koraci K`, `--runde R`, `--bez-jezgara`.
Neoptimizirana verzija je M3 kod (commit `73ceb9e`), kopiran u `demo/M3Solver.inc` i preveden dvaput
(`demo/m3_b0.cpp` bez vektorizacije, `demo/m3_b1.cpp` sa zadanim oznakama); Optimizirana verzija je
`smoke::SmokeSolver`. Računar za vrijeme mjerenja treba biti miran.

`smoke_lab` (npr. `Solver/build/smoke_lab --grid 64`, ili samo jedno jezgro sa `--kernel jacobi` /
`--kernel advekcija`) traje nekoliko sekundi i pokazuje ubrzanje **jednog jezgra** (Jacobi i advekcija). Ubrzanje cijelog koraka, mjereno po protokolu iz `../docs/plan/OPTIMIZATION.md` (odjeljak 4,
odluka D10), je u `../docs/plan/PERF_LOG.md`.

Primjeri: `smoke_cli --grid 128 --steps 200`, `smoke_cli --nx 64 --ny 128 --nz 64 --quiet`.
Benchmark: `Solver/build/smoke_bench --steps 100 --warmup 10 --output perf.csv`;
kalibracija: `Solver/build/smoke_bench --calibrate`; hardver i opcije gradnje: `Solver/build/smoke_bench --hwinfo`;
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
