# PERF_LOG: evidencija optimizacija i mjerenja

Pravila vođenja su u [OPTIMIZATION.md](OPTIMIZATION.md). Ukratko: **jedan unos po izmjeni, i
kad ne pomaže**; mjerenje u istoj sesiji sa osnovom; glavne metrike su ubrzanje **S** i delta **Δ**.
Strojno čitljiva kopija je `perf/results.csv`. Oba fajla se mijenjaju u istom commitu kao i kod.

Prvi unosi su osnove **B0** i **B1**, izmjerene 2026-11-09 na istom računaru. Svi pojedinačni
uzorci po fazi su u `perf/results.csv`.

---

## 1. Otisci računara tima

Popuniti jednom po računaru, a promijeniti kad se promijeni nešto od ovoga (drajver, kompajler,
plan napajanja).

| Oznaka | Vlasnik | Procesor | Jezgara/niti | L2 / L3 | RAM | AVX2 | Kompajler | Napomena |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| PC-1 | Adi | Apple M4 | 10/10 | L2 4 MiB / L3 nije prijavljen | 16 GiB | AVX2 ne, ARM NEON da | AppleClang 21.0.0.21000101 | macOS 25.6.0 |
| PC-2 | | | | | | | | |
| PC-3 | | | | | | | | |

Komanda za procesor (PowerShell):
`Get-CimInstance Win32_Processor | Select Name,NumberOfCores,NumberOfLogicalProcessors,L2CacheSize,L3CacheSize`

---

## 2. Šablon unosa (kopirati za svaku izmjenu)

```
### P-XXX: <kratak naslov>

- Datum / autor / računar: <YYYY-MM-DD> / <ime> / <PC-n>
- Commit: <kratki hash> (osnova: <kratki hash>)
- Nivo i tehnika: <nivo 1, T8 omp simd | nivo 2, OpenMP niti | ...>
- Jezgro / faza: <npr. Projection::jacobi>
- Hipoteza: <šta očekujem i zašto>
- Parametri: mreža <64^3>, Jacobi iteracija <40>, niti <1>, koraka <100 + 10 zagrijavanja>
- Gradnja: <Release>, oznake <-O2 ...>, kompajler <g++ 16.2.0>
- Uslovi: <priključen, visoke performanse, drugi programi zatvoreni>

| Varijanta | Medijan (ms) | Min (ms) | Raspon (ms) | GB/s | E |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Osnova | | | | | |
| Nova | | | | | |

- Ubrzanje S = T_osnova / T_nova =
- Delta Δ = (T_osnova - T_nova) / T_osnova =
- Tačnost: najveća razlika prema referentnom rezultatu = <vrijednost>, testovi <prolaze / ne prolaze>
- Zaključak: <ZADRŽATI | ODBACITI | JOŠ MJERITI>
- Šta smo naučili: <jedna do dvije rečenice, i kad nije pomoglo>
```

---

## 3. Unosi

| ID | Datum | Računar | Nivo / tehnika | Jezgro | Mreža | Niti | S | Δ | Zaključak |
| :--- | :--- | :--- | :--- | :--- | :--- | ---: | ---: | ---: | :--- |
| P-001 | 2026-11-09 | PC-1 | M3 / bez vektorizacije | svi koraci | 32³–128³ | 1 | — | — | baseline |
| P-002 | 2026-11-09 | PC-1 | M3 / compiler default | svi koraci | 32³–128³ | 1 | 1.58–1.64x | 36.55–38.89% | baseline |

(Ispod tabele idu puni unosi po šablonu, najnoviji na kraju.)

### P-001: B0 serijska osnova bez vektorizacije

- Datum / autor / računar: 2026-11-09 / Adi / PC-1
- Commit: radno stablo zasnovano na `58d0e18` (M3 izmjene nisu commitane)
- Nivo i tehnika: osnova B0, kompajlerska vektorizacija isključena
- Jezgro / faza: cijeli `SmokeSolver::step`, vremena po fazi
- Parametri: 32³, 64³, 96³, 128³; K=40; dt=1/60; ε=5; 10 zagrijavanja + 100 mjerenih koraka; jedna nit
- Gradnja: Release, AppleClang 21.0.0.21000101, `-O3 -DNDEBUG -fno-vectorize -fno-slp-vectorize`
- Otisak: Apple M4, 10 hardverskih niti, 16 GiB RAM; ARM NEON; L2 4 MiB; L3 nije prijavljen.

| Mreža | Izvor (ms) | Sile (ms) | Advekcija brzine (ms) | Projekcija (ms) | Advekcija skalara (ms) | Ukupno (ms) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| 32³ | 0.007791 | 0.217959 | 0.561250 | 1.971563 | 0.374854 | 3.140813 |
| 64³ | 0.037604 | 1.714521 | 4.663250 | 15.060250 | 3.065021 | 24.569354 |
| 96³ | 0.106792 | 6.537209 | 15.334167 | 49.459896 | 10.256250 | 81.797000 |
| 128³ | 0.298479 | 15.693500 | 36.161250 | 118.246688 | 24.190709 | 194.667542 |

### P-002: B1 serijska osnova s podrazumijevanim optimizacijama

- Datum / autor / računar: 2026-11-09 / Adi / PC-1
- Commit: radno stablo zasnovano na `58d0e18` (M3 izmjene nisu commitane)
- Nivo i tehnika: osnova B1, podrazumijevana vektorizacija kompajlera
- Jezgro / faza: cijeli `SmokeSolver::step`, vremena po fazi
- Parametri: isti scenarij kao P-001; rezultati su medijani 100 uzoraka po fazi.
- Gradnja: Release, AppleClang 21.0.0.21000101, `-O3 -DNDEBUG`
- Kalibracija: B1 memorijski triad 50.365 GB/s, zavisni lanac 1.680 ns/operaciju; B0 triad
  57.007 GB/s, lanac 1.662 ns/operaciju. Ovo su približne vrijednosti: ponovljena kalibracija je
  varirala, pa se `E` ne koristi kao kriterij za zaključke iz M3.
- Uslovi: plan napajanja i pozadinsko opterećenje nisu posebno zabilježeni; rasponi ostaju u CSV-u.

| Mreža | Izvor (ms) | Sile (ms) | Advekcija brzine (ms) | Projekcija (ms) | Advekcija skalara (ms) | Ukupno (ms) | `f` projekcije |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32³ | 0.006125 | 0.215146 | 0.558875 | 0.834875 | 0.371688 | 1.992979 | 41.89% |
| 64³ | 0.029125 | 1.637146 | 4.570938 | 5.622500 | 3.049812 | 15.014208 | 37.45% |
| 96³ | 0.092063 | 5.214584 | 15.411188 | 19.429500 | 10.253250 | 50.357813 | 38.58% |
| 128³ | 0.257750 | 11.999833 | 36.206334 | 47.546917 | 24.254750 | 120.484749 | 39.46% |

| Mreža | Ubrzanje cijelog koraka B0/B1 | Δ vremena | Projekcija najveća faza |
| :--- | ---: | ---: | ---: |
| 32³ | 1.576x | 36.55% | da |
| 64³ | 1.636x | 38.89% | da |
| 96³ | 1.624x | 38.44% | da |
| 128³ | 1.616x | 38.11% | da |

B0 i B1 dali su bitno identičan rezultat na svim mrežama (maksimalna razlika gustoće 0). Najveća
razlika u vremenu je u projekciji: B1 je 2.36–2.68x brži po toj fazi, dok advekcija brzine/skalara
ostaje približno jednaka. To potvrđuje da je projekcija dobar prvi cilj za M4; mjereni rasponi
(`min_ms`, `max_ms`) ostaju u CSV-u. Efikasnost E u CSV-u je modelirana propusnost po Jacobi
iteraciji podijeljena memorijskim triadom; vrijednost može preći 1 zbog ponovnog korištenja podataka
iz cachea i procjene bajtova po ćeliji.

---

## 4. Šema `perf/results.csv`

Jedna linija po mjerenom jezgru i varijanti u jednoj sesiji. Zaglavlje:

```
entry_id,date,machine,commit,base_commit,level,technique,stage,grid_nx,grid_ny,grid_nz,jacobi_iters,threads,steps,build_type,compiler,flags,variant,median_ms,min_ms,max_ms,gbps,efficiency_E,speedup_S,delta_pct,max_abs_diff,verdict
```

- `variant`: npr. `B0`, `B1`, `restrict`, `omp_simd`, `avx2`.
- `speedup_S` i `delta_pct` se računaju prema osnovi **iz iste sesije**.
- `max_abs_diff`: najveća razlika prema referentnom rezultatu (VERIFICATION.md, odjeljak 3).
- `verdict`: `keep`, `drop` ili `more`.
- Fajl se samo dopunjava; stari redovi se ne brišu.
