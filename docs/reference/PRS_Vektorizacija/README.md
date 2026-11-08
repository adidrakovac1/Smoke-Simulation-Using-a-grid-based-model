# PRS_Vektorizacija (referentni laboratorijski primjer)

Laboratorijska vježba `lab1_perf_simd` o mjerenju i vektorizaciji (izvorni arhiv `PRS_Vektorizacija.7z`).
**Ovo nije dio našeg Solvera i ne gradi se s njim.** Čuva se kao uzor za optimizaciju. Raspakovano
bez izmjena.

Šta sadrži i kako se to koristi u projektu: `docs/plan/OPTIMIZATION.md`, odjeljak 2 (tabela tehnika
T1 do T17 i njihovo preslikavanje na naša jezgra).

| Fajl | Sadržaj |
| :--- | :--- |
| `include/bench.hpp`, `src/bench.cpp` | mjerenje (zagrijavanje, ponavljanja, medijan, minimum), `consume`, `checksum`, detekcija AVX2 |
| `include/measurement_kernels.hpp` | zavisni i nezavisni lanci operacija (ILP) |
| `include/memory_kernels.hpp` | prolazak po redovima i kolonama, redoslijed petlji u množenju matrica, blokiranje (tiling) |
| `include/vectorization.hpp`, `src/vectorization_*.cpp` | SAXPY, skalarni proizvod, prag: scalar, auto, `omp simd`, AVX2; `restrict`; prefix scan; escape-time |
| `include/particle_update.hpp`, `src/particle_update_*.cpp` | AoS naspram SoA, grananje naspram bez grananja, AVX2 |
| `include/simd_support.hpp` | poravnata alokacija (`AlignedFloats`), `check_vector` |
| `src/benchmark_runner.cpp` | sastavlja sva mjerenja i provjere tačnosti |
| `CMakeLists.txt` | `-fno-tree-vectorize` za skalarne fajlove, `-mavx2` po fajlu, OpenMP, izvještaji vektorizacije |

Napomene pri korištenju:
- Njegov `CMakeLists.txt` ne postavlja nivo optimizacije. Za smislena mjerenja gradi se s `-DCMAKE_BUILD_TYPE=Release`.
- Postavlja C++23 i MSVC zastavice; na našem MinGW-u (g++) treba provjeriti pri prvom gradnji.
- U arhivi nema rezultata mjerenja.
