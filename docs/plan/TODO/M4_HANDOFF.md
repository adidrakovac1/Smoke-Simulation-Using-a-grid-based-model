# M4 handoff: status i preostali posao

Ovaj dokument je predaja rada za člana tima koji nastavlja M4. M4 je **u toku**: M4a nije iscrpljen,
M4b nije počeo, a M4c nije urađen. M4 ostaje sekvencijalan, bez CPU niti i GPU-a (odluka D8).

## Trenutno stanje repozitorija

- Grana: `feature/m4`
- Osnova/HEAD pri predaji: `73ceb9e` (`feat(solver): add vorticity confinement, smoke_bench and M3 baselines`)
- Sve M4 izmjene s PC-1 su commitane u `eeb591d` (`docs(m4): add serial optimization handoff`).
- **Nastavak (2026-11-09):** M4 preuzima Ermin na PC-2 (odluka D9). PC-1 brojevi ne služe kao osnova
  za PC-2; referenca je sada po računaru (`serial_64_100steps_pc1.bin`, `serial_64_100steps_pc2.bin`).
- M4 checklist u [PLAN.md](../PLAN.md) i dalje je neoznačen, ispravno: nijedan milestone nije zatvoren.

## Šta je urađeno

### Mjerenja i profil

- Ponovo su izmjerene B0 i B1 osnove na Apple M4 za 32³, 64³, 96³ i 128³ mreže; M4 mjerenja su
  serijska. B0 isključuje kompajlersku vektorizaciju, a B1 koristi podrazumijevane optimizacije.
- Napravljen je AppleClang izvještaj o automatskoj vektorizaciji. Divergencija, Jacobi, korekcija
  gradijenta i uzgon vektoriziraju se (širina 4). Semi-Lagrangian advekcija se ne vektorizira zbog
  trilinearnih čitanja na adresama zavisnim od brzine.
- `i` je već najunutrašnja petlja u važnim jezgrima; promjena redoslijeda petlji nije bila potrebna.
- Detalji mjerenja i zaključci M4-001 do M4-005 su u [PERF_LOG.md](../PERF_LOG.md); pojedinačni
  strojno čitljivi rezultati su u [perf/results.csv](../../../perf/results.csv).

### Zadržana izmjena

- **M4-003 / T10: branchless vorticity confinement**, zadržan za zadanu B1 gradnju.
- Kompajler sada vektorizira drugi prolaz confinementa.
- Faza Sile je ubrzana oko 24–33% na B1, a cijeli korak oko 2.6–3.3%.
- Na B0 je faza Sile bila oko 9–14% sporija; korist se odnosi na B1, ne na neoptimiziranu osnovu.
- Dodan je test za slučaj nultog gradijenta. Referentna provjera na 64³/100 koraka daje
  `max_abs_diff=1.95503e-5`, relativnu grešku `1.50863e-6`, ispod praga `1e-4`.

### Odbačeni pokušaji

- **M4-001 / T12:** `restrict` i eksplicitni linearni indeksi u projekciji — odbačeno; razlika nije
  nadmašila raspon mjerenja osnove.
- **M4-002 / T12:** `restrict`/linear indexing u advekciji — odbačeno; rezultati su bili unutar šuma.
- **M4-004 / T6:** Jacobi blokiranje 4×8 — odbačeno; Jacobi iteracija bila je 9–14% sporija, a cijeli
  korak 3.6–5.3% sporiji.
- **M4-005 / T12:** `restrict` i linearni indeksi u uzgonu — kod je vraćen; faza Sile nije ubrzana.
  Rezultati za sve četiri mreže su zabilježeni.

## Izmijenjeni fajlovi

- [Solver/bench/bench_main.cpp](../../../Solver/bench/bench_main.cpp): `--verify` provjerava relativnu
  grešku do `1e-4`, odbija nekonačna stanja i vraća neuspjeh kad provjera ne prođe.
- [Solver/src/stages/Forces.cpp](../../../Solver/src/stages/Forces.cpp): zadržana branchless
  vorticity izmjena; neuspjeli buoyancy eksperiment je uklonjen.
- [Solver/tests/test_vorticity.cpp](../../../Solver/tests/test_vorticity.cpp): test nultog gradijenta.
- [docs/plan/VERIFICATION.md](../VERIFICATION.md): dokumentovano ponašanje `--verify`.
- [docs/plan/PERF_LOG.md](../PERF_LOG.md) i [perf/results.csv](../../../perf/results.csv): M4 pokušaji
  i rezultati. CSV je provjeren: 108 zapisa, 27 kolona, jedinstveni ID-jevi i ujednačena širina redova.

## Ograničenja računara i već poznati nalazi

Mjerenja su na Apple M4/ARM, macOS 25.6.0, AppleClang 21.0.0.21000101, Release `-O3 -DNDEBUG`.

- AVX2 nije dostupan na ARM M4.
- AppleClang u ovom okruženju odbija `-fopenmp`, a `-fopenmp-simd` nije omogućio `omp simd`.
- Ovo su ograničenja PC-1/kompajlera, ne dokaz da tehnike nisu dostupne na drugim računarima.
- `Field3D` već koristi 64-bajtno poravnanje baze i pitch redova višekratnik od 8 floatova. Testovi
  provjeravaju alokaciju/poravnanje, ali uporedno mjerenje s neporavnatom varijantom još nije urađeno.
- Jacobi tiling je izmjeren i odbačen. Nije potrebno ponavljati isti 4×8 pokušaj bez nove hipoteze.

## Šta ostaje uraditi

1. Nastaviti M4a po jezgrima iz najskupljih faza: Jacobi, divergencija/korekcija, advekcija, uzgon i
   redukcije. Svaki primjenjiv pokušaj prvo provjeriti tačnošću, zatim mjeriti i evidentirati.
2. Preostale stavke nivoa 1 procijeniti i zapisati po jezgru: poravnanje kao uporedni eksperiment,
   branchless transformacije gdje postoje grananja, te više akumulatora/redukcije gdje su primjenjivi.
   Odbaciti stavke koje nisu primjenjive uz obrazloženje; ne uvoditi OpenMP niti.
3. Nastaviti dopunjavati `PERF_LOG.md` i `perf/results.csv` za svaki pokušaj, uključujući neuspješne.
   Zadržati naizmjenično mjerenje osnove i kandidata te poređenje prema rasponu šuma.
4. Ažurirati checklistu u `PLAN.md` tek kada su koraci zaista završeni i validirani.
5. M4b početi tek po iscrpljenju M4a i odobrenju tima. M4c zahtijeva pregled, tačnost, tabele vremena
   i ubrzanja prema B0/B1 te provjeru `SlabRange`.
6. Prije predaje/commita ponoviti testove, referentnu provjeru, `git diff --check` i provjeru CSV-a.

## Validacija do sada

Prošlo je:

```sh
cmake --build Solver/build --target smoke_bench smoke_tests --parallel
ctest --test-dir Solver/build --output-on-failure
Solver/build/smoke_bench --verify Solver/reference/serial_64_100steps_pc1.bin --steps 100   # na PC-1
git diff --check
```

Posljednja izmjena buoyancy kernelu je vraćena, a build, `ctest`, `--verify` i `git diff --check`
ponovo su prošli nakon vraćanja.

