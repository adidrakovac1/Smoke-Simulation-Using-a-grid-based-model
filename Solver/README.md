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
| `smoke_bench` | mjerenje (zasad prazan stub, M3) |

Primjeri: `smoke_cli --grid 128 --steps 200`, `smoke_cli --nx 64 --ny 128 --nz 64 --quiet`.
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

## Stanje (M2)

Radi cijeli korak: izvor dima, uzgon (temperatura), advekcija brzine, projekcija (divergencija, Jacobi,
korekcija), advekcija gustoće i temperature; rubni uslovi; dijagnostika na zahtjev (`diagnostics()`);
mjerenje vremena po fazi; konzolni program (`--dt`, `--jacobi`, `--dump-slices`, `--demo advect`);
testovi T1 do T9.

**Ne radi još** (M3): vorticity confinement, `smoke_bench`, referentni rezultat, T10.
