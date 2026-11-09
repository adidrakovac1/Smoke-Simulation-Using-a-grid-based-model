# PERF_LOG: evidencija optimizacija i mjerenja

Pravila vođenja su u [OPTIMIZATION.md](OPTIMIZATION.md). Ukratko: **jedan unos po izmjeni, i
kad ne pomaže**; mjerenje u istoj sesiji sa osnovom; glavne metrike su ubrzanje **S** i delta **Δ**.
Strojno čitljiva kopija je `perf/results.csv`. Oba fajla se mijenjaju u istom commitu kao i kod.

Prvi unosi su osnove **B0** i **B1**, izmjerene 2026-11-09 na istom računaru. Svi pojedinačni
uzorci po fazi su u `perf/results.csv`.

**Važno za M4:** P-001 i P-002 su izmjereni na PC-1 (Apple M4, ARM, AppleClang). Ostaju kao
izmjereni podaci, ali **nisu osnova za M4**:
- pravilo traži poređenje s osnovom na istom računaru u istoj sesiji;
- tehnike iz laba (AVX2) na ARM-u ne postoje.

Prije prve M4 izmjene, onaj ko radi M4 mjeri B0 i B1 na svom x86 računaru, upisuje ih kao nove
unose s otiskom tog računara i snima referencu na tom računaru (VERIFICATION.md, odjeljak 3). To
radi tek kad je ε konačno izabran (MATH.md, odjeljak 3, nalaz o divergenciji), jer ε mijenja i
vremena i rezultat.

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

## 5. M4a: prva mjerenja (2026-11-09)

- Računar: PC-1 (Apple M4, 10 hardverskih niti, 16 GiB RAM), macOS 25.6.0.
- Gradnja: Release, AppleClang 21.0.0.21000101, B1 `-O3 -DNDEBUG`; serijski, jedna nit.
- Protokol: ista sesija; 10 zagrijavanja + 100 uzoraka po mreži, medijan po koraku.
- Osnova: commit `73ceb9e` (M3), ponovo izmjerena neposredno prije eksperimenta. Mjerenje uključuje
  32³, 64³, 96³ i 128³; privremeni izlazi su korišteni za poređenje, bez mijenjanja M3 referentnog fajla.

### M4-001: T12 `restrict` i eksplicitni linearni indeksi u projekciji — ODBACITI

- Jezgra: divergencija, Jacobi iteracija i oduzimanje gradijenta pritiska.
- Hipoteza: `restrict` i sirovi pokazivači mogu ukloniti provjere aliasinga i trošak `Field3D::at()`.
- Tačnost: svi `smoke_tests` prolaze; nakon 100 koraka 64³ rezultat je bit-po-bit identičan i privremenoj
  M4 osnovi i postojećoj referenci (maks. apsolutna razlika 0).
- `S = T_B1_osnova / T_restrict`; `Δ = (T_osnova - T_restrict) / T_osnova`.

| Mreža | B1 osnova projekcije (ms) | `restrict` (ms) | S | Δ | Osnovni raspon (ms) | Odluka |
| :--- | ---: | ---: | ---: | ---: | ---: | :--- |
| 32³ | 0.833667 | 0.772709 | 1.0789x | 7.31% | 0.110709 | unutar raspona |
| 64³ | 5.576938 | 5.369583 | 1.0386x | 3.72% | 0.400875 | unutar raspona |
| 96³ | 19.061563 | 18.452730 | 1.0330x | 3.19% | 3.901001 | unutar raspona |
| 128³ | 46.082896 | 45.986709 | 1.0021x | 0.21% | 4.365125 | unutar raspona |

U svim veličinama razlika medijana je manja od raspona osnove, pa nema dokaza da poboljšanje nadilazi
šum. `restrict` izmjena je uklonjena; detaljni medijani, minimumi i maksimumi eksperimenta ostaju u
`perf/results.csv`.

### Profil kompajlera i preostali koraci nivoa 1

Clang-ov `-Rpass=loop-vectorize` izvještaj na B1 potvrđuje da su unutrašnje `i` petlje već vektorizirane
(vektorska širina 4) za divergenciju, Jacobi, korekciju gradijenta i uzgon. Unutrašnja `i` petlja je već
brzom memorijskom smjeru, pa T4/T5 ne zahtijevaju promjenu redoslijeda. Semi-Lagrangian advekcija se ne
vektorizira zbog trilinearnih čitanja na adresama zavisnim od brzine; to je sljedeći prioritet nakon
projekcije. Izvještaj pokazuje da petlja s granom izvora nije vektorizirana; vorticity confinement
postaje vektoriziran nakon M4-003.

Ovo nije zatvaranje M4a: T8 (`omp simd`) i AVX2 nisu dostupni u ovom PC-1 okruženju; T6 je naknadno
izmjeren za Jacobi u M4-004. Poravnanje je dio postojeće Field3D osnove, ali poređenje s neporavnatom
varijantom još nije urađeno. M4b nije započet.

### M4-002: T12 `restrict` i eksplicitni linearni indeksi u advekciji — ODBACITI

- Jezgra: advekcija brzine i skalara.
- Hipoteza: ograničavanje izlaznog buffera i izravni linearni indeksi smanjuju trošak `Field3D::at()`.
- Tačnost: nakon korekcije indeksiranja rezultati prolaze sve testove i 64³/100-step referencu bit-po-bit
  (maks. apsolutna razlika 0). Ulazni velocity pokazivači mogu aliasati jedan s drugim; zato nisu označeni
  kao `restrict`. Izmjena je vraćena na prvobitni kod.
- B1 usporedba s osnovom; sva poboljšanja i pogoršanja su manja od raspona mjerenja osnove.

| Mreža | Advekcija brzine S / Δ | Advekcija skalara S / Δ | Cijeli korak S / Δ |
| :--- | :--- | :--- | :--- |
| 32³ | 1.0032x / +0.32% | 1.0019x / +0.18% | 0.9999x / -0.01% |
| 64³ | 1.0006x / +0.06% | 1.0015x / +0.15% | 0.9954x / -0.46% |
| 96³ | 1.0015x / +0.15% | 0.9975x / -0.25% | 0.9983x / -0.17% |
| 128³ | 1.0032x / +0.32% | 1.0035x / +0.35% | 1.0035x / +0.35% |

Odluka: nema mjerljivog ubrzanja iznad šuma; ne zadržavati promjenu. `perf/results.csv` sadrži medijan,
minimum, maksimum, S i Δ za obje advekcijske faze i ukupni korak na sve četiri mreže.

### Ograničenja tehnika na PC-1

- T8 `#pragma omp simd`: nije primjenjiv s AppleClang 21 u ovom okruženju; kompajler odbija `-fopenmp`,
  a `-fopenmp-simd` ne uključuje podršku za direktivu. Nije dodavana neprepoznata pragma.
- T9/T15/T16 AVX2: nije primjenjivo na Apple M4/ARM NEON; nema AVX2 putanje koja bi se mogla mjeriti
  na ovom računaru.
- T4/T5: u svim glavnim jezgrima `i` je već najunutrašnja petlja, što odgovara contiguous memory layoutu.

### M4-003: T10 uklanjanje grananja u vorticity confinementu — ZADRŽATI ZA B1

- Jezgro: drugi prolaz `applyVorticityConfinement`, najskuplji dio faze Sile nakon uklanjanja grananja.
- Hipoteza: zamijeniti `if (|grad| > 0)` selekcijom s konačnim nazivnikom, kako bi kompajler mogao
  vektorizirati petlju.
- Tačnost: dodan test koji potvrđuje da ćelija s nultim gradijentom magnitude ne mijenja brzinu.
  Svi testovi prolaze; na 64³/100 koraka izmjereno je 31,394 različitih ćelija,
  `max_abs_diff=1.95503e-5`, relativna greška `1.50863e-6` (prag `1e-4`), pa `--verify` prolazi.
  Namjerno oštećena referenca prelazi prag i vraća izlazni kod 1.
- Kompajlerski izvještaj potvrđuje da petlja confinementa sada vektorizira (width 4).
- Mjereno B1 osnovno/izmijenjeno naizmjenično u dvije runde; 10 warmup + 100 uzoraka svaka.
  Tabela prikazuje omjer i delta iz prosječnih medijana dviju rundi. B0 je također izmjeren kao
  kontrola, bez kompajlerske vektorizacije.

| Mreža | B1 Sila: B1 / T10 medijani (ms) | S / Δ faze Sile | B1 cijeli korak: B1 / T10 medijani (ms) | S / Δ koraka |
| :--- | :--- | :--- | :--- | :--- |
| 32³ | 0.21467 / 0.16308 | 1.316x / +24.03% | 1.98622 / 1.93389 | 1.027x / +2.63% |
| 64³ | 1.29314 / 0.89363 | 1.447x / +30.89% | 14.63254 / 14.20392 | 1.030x / +2.93% |
| 96³ | 5.01451 / 3.35268 | 1.496x / +33.14% | 49.69826 / 48.04417 | 1.034x / +3.33% |
| 128³ | 11.77480 / 8.05106 | 1.463x / +31.62% | 118.42719 / 114.58237 | 1.034x / +3.25% |

Na B0 je faza Sile bila 9–14% sporija (S=0.87–0.92), pa je ova izmjena zadržana za zadanu B1
gradnju; B0 ostaje samo mjerna osnova. Cjelokupni korak pokazuje konzistentno poboljšanje u obje
naizmjenične runde, ali pojedinačni min–max rasponi su široki zbog sistemskih prekida; ne tumačiti
male promjene cijelog koraka kao preciznije od mjerenja. Sirovi medijani/min/max i obje varijante su u
`perf/results.csv`.

### M4-004: T6 blokiranje Jacobi ravnina (4 k × 8 j) — ODBACITI

- Hipoteza: rad na malim blokovima k/j poboljšava reuse šest-susjedskih pritisaka u kešu.
- Tačnost: svi testovi prolaze, a referenca ostaje unutar M4 relativnog praga.
- Rezultat: na svim veličinama Jacobi iteracija je 9–14% sporija; projekcija je 10–13% sporija, a cijeli
  korak 3.6–5.3% sporiji. Blokiranje mijenja sekvencu obilaska i narušava linearni streaming, bez koristi
  koja bi opravdala složenost. Izmjena je vraćena.

| Mreža | Jacobi S | Jacobi Δ | Projekcija S | Cijeli korak Δ |
| :--- | ---: | ---: | ---: | ---: |
| 32³ | 0.917x | -9.08% | 0.921x | -3.59% |
| 64³ | 0.874x | -14.38% | 0.881x | -5.27% |
| 96³ | 0.893x | -11.98% | 0.899x | -4.33% |
| 128³ | 0.893x | -12.04% | 0.900x | -4.34% |

### M4-005: T12 `restrict` i eksplicitni linearni indeksi u uzgonu — ODBACITI

- Hipoteza: sirovi pokazivači i unaprijed izračunati linearni indeksi smanjuju trošak `Field3D::at()`.
- Tačnost: svi `smoke_tests` prolaze; 64³/100-step `--verify` prolazi uz
  `max_abs_diff=1.95503e-5` i relativnu grešku `1.50863e-6` prema M3 referenci.
- Mjereno prema B1 T10 stanju, u istoj sesiji, 10 zagrijavanja + 100 uzoraka po mreži. Faza `forces`
  uključuje i uzgon i vorticity confinement.

| Mreža | Sile: osnova / nova (ms) | S | Δ | Cijeli korak: osnova / nova (ms) | S | Δ |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| 32³ | 0.162729 / 0.163813 | 0.993x | -0.67% | 1.925543 / 1.933416 | 0.996x | -0.41% |
| 64³ | 0.866938 / 0.895062 | 0.969x | -3.24% | 14.150896 / 14.176875 | 0.998x | -0.18% |
| 96³ | 3.308646 / 3.376229 | 0.980x | -2.04% | 47.857021 / 47.816147 | 1.001x | +0.09% |
| 128³ | 8.091000 / 8.176042 | 0.990x | -1.05% | 114.811478 / 115.356645 | 0.995x | -0.47% |

Uzgon nije ubrzan ni na jednoj mreži, a faza Sile je sporija za 0.7–3.2%; sitna razlika ukupnog
koraka na 96³ je unutar varijacije mjerenja. Izmjena je vraćena. Sirovi medijani, minimumi i maksimumi
su sačuvani u `perf/results.csv`.

T11 provjera: prije M4, `Field3D` je već koristio 64-bajtno poravnanje i redni pitch zaokružen na
višekratnik od osam floatova; postojeći testovi provjeravaju poravnanje baze, pitch i indekse. Ovo
potvrđuje ispravnost rasporeda memorije, ali ne mjeri učinak u odnosu na neporavnati raspored, pa T11
se ne smatra uporedno iscrpljenim.
