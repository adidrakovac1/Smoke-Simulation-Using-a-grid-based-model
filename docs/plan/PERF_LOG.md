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

**Stanje (2026-11-09):** ε = 5 je konačan (odluka D9). M4 nastavlja Ermin na **PC-2**; PC-2 osnove i
referenca su u odjeljku 6. Unosi M4-001 do M4-005 (odjeljak 5) vrijede za PC-1 i AppleClang.

---

## 1. Otisci računara tima

Popuniti jednom po računaru, a promijeniti kad se promijeni nešto od ovoga (drajver, kompajler,
plan napajanja).

| Oznaka | Vlasnik | Procesor | Jezgara/niti | L2 / L3 | RAM | AVX2 | Kompajler | Napomena |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| PC-1 | Adi | Apple M4 | 10/10 | L2 4 MiB / L3 nije prijavljen | 16 GiB | AVX2 ne, ARM NEON da | AppleClang 21.0.0.21000101 | macOS 25.6.0 |
| PC-2 | Ermin | AMD Ryzen 5 5500 (Zen 3) | 6/12 | L2 6×512 KiB / L3 16 MiB | 32 GiB | AVX2 da, FMA da, AVX-512 ne | g++ 16.2.0 (MSYS2 UCRT64) | Windows 11 Pro 10.0.26300, plan napajanja High performance |
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

Napomena: odluke M4-001 do M4-005 donesene su po starom pravilu šuma (max − min svih koraka), na PC-1.
Od D10 vrijedi novo pravilo (OPTIMIZATION.md, odjeljak 4, tačka 5); PC-1 unosi se ne prepravljaju.

---

## 6. PC-2: osnove i nastavak M4 (2026-11-09)

Zajedničko za sve unose u ovom odjeljku:
- **Računar:** PC-2 (odjeljak 1), plan napajanja High performance. Pozadinsko opterećenje nije
  posebno kontrolisano; korisnik je zamoljen da ne koristi računar tokom mjerenja.
- **Gradnja:** Release, g++ 16.2.0 (MSYS2 UCRT64). B1 je `-O3 -DNDEBUG`, B0 je
  `-O3 -DNDEBUG -fno-tree-vectorize`.
- **Scenarij:** ε = 5, K = 40, dt = 1/60, jedna nit.
- **Protokol (D10):** `perf/run_rounds.sh`, 3 naizmjenične runde, svaka 10 zagrijavanja + 100 mjerenih
  koraka. Glavni broj je medijan medijana rundi. Šum je raspon medijana rundi osnove. Presude računa
  `perf/summarize_rounds.sh`.
- **Sesije:** 32³, 64³ i 128³ iz sesije 1. 96³ iz ponovljene sesije, jer je u sesiji 1 jedna runda
  M4-B0 bila poremećena (cijeli korak 352 ms umjesto oko 85 ms).
- **Tačnost:** svaka varijanta je prošla 42 ili 43 testa i `--verify` prema
  `serial_64_100steps_pc2.bin`, bit po bit isto (razlika 0).
- **Kalibracija:** `--calibrate` daje triad oko 107 GB/s, a teorijski maksimum dvokanalnog DDR4
  je oko 51 GB/s. Mjerenje kalibracije je vjerovatno pogrešno (nije provjereno), pa se **E ne
  računa** dok se ne provjeri.

### P2-001 / P2-002: osnove B0 i B1 na PC-2 (commit `73ceb9e`)

Medijani u ms po koraku, B0 / B1:

| Mreža | Izvor | Sile | Advekcija brzine | Projekcija | Advekcija skalara | Ukupno | S koraka (B0/B1) | `f` projekcije (B1) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32³ | 0.0066 / 0.0048 | 0.286 / 0.267 | 0.618 / 0.618 | 1.345 / 0.673 | 0.515 / 0.514 | 2.787 / 2.094 | 1.331x | 32.2% |
| 64³ | 0.034 / 0.029 | 2.518 / 2.431 | 5.274 / 5.331 | 10.854 / 5.540 | 3.778 / 3.784 | 22.617 / 17.230 | 1.313x | 32.2% |
| 96³ | 0.145 / 0.130 | 12.324 / 9.726 | 17.752 / 17.807 | 37.367 / 19.119 | 12.140 / 12.202 | 81.670 / 59.171 | 1.380x | 32.3% |
| 128³ | 0.449 / 0.422 | 22.000 / 21.986 | 42.406 / 42.361 | 95.359 / 59.710 | 28.910 / 28.961 | 190.822 / 156.326 | 1.221x | 38.2% |

Nalazi:
- **Projekcija:** kompajler je sam vektorizira. Projekcija je u B1 1.6x (128³) do 2.0x (32³) brža,
  a Jacobi iteracija 1.65x do 2.2x. Na 128³ je dobitak manji jer polja više ne staju u L3 (16 MiB).
- **Advekcija** se ne vektorizira: S = 1.00 na svim mrežama, isto kao na PC-1.
- **Profil B1:** projekcija je najveća faza (32% do 38%). Odmah iza je advekcija brzine (27% do 31%),
  pa advekcija skalara i sile. Redoslijed jezgara za M4a na PC-2 je zato projekcija/Jacobi, pa
  advekcija.
- **96³ je na PC-2 bučna i u ponovljenoj sesiji.** Medijan runde faze Sile za B0 kreće se od 9.6 do
  13.9 ms, a projekcije za B0 od 37.3 do 45.3 ms. Zaključci na 96³ zato imaju najmanju težinu;
  uzrok nije istražen.

### P2-003: M4-003 (branchless vorticity confinement, Adi) ponovo izmjeren na PC-2: ZADRŽATI

- Osnova je B1 na `73ceb9e`, kandidat B1 na `c8f775d` (kod isti kao `eeb591d`). Kontrola: B0 obje verzije.
- **Tačnost:** na PC-2 je rezultat **bit po bit isti** kao M3. Razlika na PC-1 (1.5·10⁻⁶) je dakle
  posljedica drugačijeg kompajliranja na ARM-u, a ne promjene računa.

| Mreža | Sile: osnova / nova (ms) | S / Δ faze Sile | Presuda (D10) | Cijeli korak S / Δ | Presuda (D10) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 32³ | 0.267 / 0.222 | 1.205x / 17.0% | brže | 1.014x / 1.4% | brže |
| 64³ | 2.431 / 1.967 | 1.236x / 19.1% | brže | 1.035x / 3.4% | brže |
| 96³ | 9.726 / 9.409 | 1.034x / 3.3% | šum | 0.961x / −4.1% | šum |
| 128³ | 21.986 / 18.338 | 1.199x / 16.6% | brže | 1.039x / 3.7% | šum |

- **Kontrola B0:** faza Sile je i bez vektorizacije brža, S = 1.19 do 1.40. Na PC-1 je bila sporija.
- **Odluka:** zadržati. Faza Sile je mjerljivo brža na 32³, 64³ i 128³. Na 96³ je presuda šum
  (vidi gore). Cijeli korak dobija 1.4% do 3.7%, a na 128³ to je unutar šuma.
- Svi redovi su u `perf/results.csv` (oznake `PC2-20261109-BASE-*` i `PC2-20261109-M4-003-*`).

### M4-006: T10 uklanjanje suvišnih odabira u M4-003: ZADRŽATI (jednostavnije, bez ubrzanja)

- **Jezgro:** drugi prolaz `applyVorticityConfinement` (`Solver/src/stages/Forces.cpp`).
- **Hipoteza:** kad je gradijent nula, onda su `nx = ny = nz = 0`, pa je sila tačno 0. Zato su
  `hasGradient ? u + f : u` (tri puta) i tri dodatna čitanja brzine suvišni; dovoljan je `+=`.
  Manje operacija bi moglo malo ubrzati petlju.
- **Tačnost:** 43/43 testa; `--verify` prema PC-2 referenci daje bit po bit isto.
- **Mjerenje:** osnova je M4-003 (`18df554`), 3 naizmjenične runde.

| Mreža | Sile: osnova / nova (ms) | S / Δ | Cijeli korak S / Δ | Presuda (D10) |
| :--- | :--- | :--- | :--- | :--- |
| 32³ | 0.219 / 0.221 | 0.993x / −0.75% | 1.000x / −0.05% | šum |
| 64³ | 1.890 / 1.894 | 0.998x / −0.23% | 0.997x / −0.35% | šum |
| 96³ | 9.340 / 8.543 | 1.093x / 8.5% | 1.042x / 4.1% | šum (96³ bučna) |
| 128³ | 17.807 / 17.446 | 1.021x / 2.0% | 0.998x / −0.18% | šum |

- **Odluka:** ubrzanja nema. Kompajler je odabire vjerovatno već sveo na isto. Izmjena je ipak
  zadržana, jer kod čini jednostavnijim (manje operacija i čitanja), a rezultat je isti.
  Oznake u CSV-u: `PC2-20261109-M4-006-*`, presuda `keep_simpler`.

### Profil kompajlera za projekciju na PC-2 (T7, g++ `-fopt-info-vec`)

- Divergencija i Jacobi **jesu** vektorizirani, ali samo s 16-bajtnim vektorima (SSE, 4 floata), jer
  x86-64 bez `-mavx2` ima samo SSE2. Uz to su "versioned for possible aliasing": u toku rada se
  provjerava da li se nizovi preklapaju.
- `subtractPressureGradient` **nije** vektoriziran ("evolution of base is not affine"). Petlja piše u
  u, v i w, pa kompajler ne može dokazati da ti upisi ne mijenjaju pokazivače unutar `Field3D`.
- T4/T5: `i` je najunutrašnja petlja u sva tri jezgra, pa se redoslijed petlji ne mijenja.

### M4-007: T12 `restrict` u jezgrima projekcije: ZADRŽATI

- **Izmjena:** unutrašnja petlja svakog jezgra (divergencija, Jacobi, gradijent) je izdvojena u
  funkciju po redu. Parametri te funkcije su `SMOKE_RESTRICT` pokazivači (`Solver/src/Restrict.h`).
  Prvi pokušaj, `restrict` na lokalnim pokazivačima, g++ je ignorisao: izvještaj se nije promijenio.
- **Profil poslije izmjene:** sve tri petlje su vektorizirane, i to bez provjere preklapanja.
- **Tačnost:** 43/43 testa; `--verify` daje bit po bit isto.
- **Mjerenje:** osnova je M4-006 (`b01216e`). Za 32³ do 96³ su 3 runde, za 128³ 5 rundi (s 3 runde
  presuda je bila "mixed", pa je 128³ ponovljena).

| Mreža | Projekcija: osnova / nova (ms) | Projekcija S / Δ | Jacobi iter. S | Cijeli korak S / Δ | Presude (proj. / Jacobi / korak) |
| :--- | :--- | :--- | ---: | :--- | :--- |
| 32³ | 0.681 / 0.558 | 1.220x / 18.0% | 1.178x | 1.064x / 6.0% | brže / brže / brže |
| 64³ | 5.402 / 4.709 | 1.147x / 12.8% | 1.109x | 1.062x / 5.8% | brže / brže / šum |
| 96³ | 25.828 / 17.533 | 1.473x / 32.1% | 1.110x | 1.022x / 2.2% | brže / šum / šum |
| 128³ | 59.585 / 56.099 | 1.062x / 5.9% | 1.049x | 1.024x / 2.4% | šum / šum / šum (brže u svih 5 rundi) |

- **96³:** S = 1.47 za projekciju je vjerovatno precijenjen. Osnova na 96³ je bučna (inače je
  oko 19 do 22 ms), a pouzdaniji je S Jacobi iteracije od 1.11.
- **128³:** polja ne staju u L3, pa je jezgro ograničeno memorijom. Uklanjanje provjere preklapanja
  tamo ne pomaže pouzdano, a usporenja nema.
- **Odluka:** zadržati. Na 32³ i 64³ je ubrzanje jasno i ponovljivo (Jacobi 1.11 do 1.18x).
  Oznake u CSV-u: `PC2-20261109-M4-007-*`.
- **Razlika od M4-001 na PC-1:** tamo je ista ideja odbačena po starom pravilu šuma, a i AppleClang
  je kod već vektorizirao bez nje. Na g++ je `restrict` na parametrima ono što omogućava
  vektorizaciju gradijenta i uklanja provjeru preklapanja.

### M4-008: T8 `#pragma omp simd` u jezgrima projekcije: NIJE PRIMIJENJENO (isti vektorski kod)

- **Hipoteza:** `#pragma omp simd` (uz `-fopenmp-simd`, bez niti) na tri petlje po redu bi mogao
  dati bolju vektorizaciju.
- **Provjera bez mjerenja vremena:** `Projection.cpp` je preveden s pragmom i bez nje (`g++ -O3 -S`).
  Obje verzije imaju **isti broj svake vektorske instrukcije** (14 `addps`, 20 `subps`, 8 `mulps`,
  2 `divps`, 32 `movups`, 5 `movlps`, 3 `shufps`, 1 `movaps`), uz isti izvještaj vektorizacije
  (16-bajtni vektori, unroll 4). Razlikuje se samo raspored registara u pripremi izvan petlje.
- **Odluka:** ne primjenjivati. Poslije M4-007 kompajler već zna da smije vektorizirati, pa pragma
  ne donosi ništa, a traži dodatnu opciju kompajlera. Vrijeme nije mjereno, jer isti kod petlje ne
  može dati drugačije vrijeme.
- **Zapažanje za kasnije:** Jacobi dijeli sa 6 (`divps`). Množenje sa `1/6` je brže, ali mijenja
  zaokruživanje (rezultat više nije bit po bit isti). Kandidat je za M4b, uz `--verify` s pragom.

### M4-009: T15/T16 AVX2 po fajlu za Jacobi, s provjerom procesora: ZADRŽATI

- **Izmjena:**
  - Tijelo Jacobi petlje je u `Solver/src/stages/JacobiRows.inc`. Prevodi se dvaput: u
    `Projection.cpp` sa zadanim opcijama i u `Projection_avx2.cpp` sa `-mavx2` (samo taj fajl, T16).
  - `jacobiIteration` pri izvršavanju bira AVX2 kopiju ako `avx2Supported()`
    (`Solver/src/CpuFeatures.cpp`, T15, iste provjere kao `avx2_supported()` iz laba) vrati true.
    Inače koristi zadanu kopiju (rezervna putanja).
  - CMake opcija `SMOKE_ENABLE_AVX2` (zadano ON) radi samo na x86. U B0 gradnji je isključena.
- **Zamka koju dizajn izbjegava:** AVX2 fajl ne uključuje zajedničke inline funkcije (`Field3D`).
  Inače bi linker mogao zadržati AVX2 kopiju za cijeli program, pa bi se program srušio na procesoru
  bez AVX2.
- **Profil:** AVX2 kopija je vektorizirana s 32-bajtnim vektorima (8 floatova, unroll 8), a ostatak
  reda sa 16-bajtnim. Nema FMA instrukcija (`-mavx2` ne uključuje FMA), pa je aritmetika ista.
- **Tačnost:**
  - novi test `jacobi_avx2_and_scalar_builds_are_bit_identical` (nx = 13, pokriva i ostatak reda)
    potvrđuje da su AVX2, skalarna i automatski izabrana putanja bit po bit iste;
  - 44/44 testa; `--verify` daje bit po bit isto.
- **Mjerenje:** osnova je M4-008 (`f1f7149`), 3 runde.

| Mreža | Jacobi iter.: osnova / nova (ms) | Jacobi S | Projekcija S / Δ | Cijeli korak S / Δ | Presude (Jacobi / proj. / korak) |
| :--- | :--- | ---: | :--- | :--- | :--- |
| 32³ | 0.01281 / 0.00934 | 1.372x | 1.337x / 25.2% | 1.061x / 5.7% | brže / brže / brže |
| 64³ | 0.1063 / 0.0774 | 1.375x | 1.340x / 25.4% | 1.074x / 6.9% | brže / brže / brže |
| 96³ | 0.3729 / 0.2487 | 1.500x | 1.376x / 27.3% | 1.152x / 13.2% | brže / brže / šum |
| 128³ | 1.2816 / 1.1512 | 1.113x | 1.101x / 9.2% | 1.038x / 3.7% | brže / brže / brže |

- **Tumačenje:**
  - Na 32³ do 96³ Jacobi je 1.37 do 1.50x brži, a ne 2x, iako je vektor dvostruko širi. Dio
    vremena odlazi na čitanja iz memorije (6 susjeda, od kojih su `i±1` neporavnata) i na
    `setBoundary` poslije svake iteracije.
  - Na 128³ je dobitak 1.11x. Polja tamo ne staju u L3, pa računanje više nije usko grlo. E se ne
    računa dok se ne provjeri kalibracija (vidi uvod odjeljka 6).
- **Odluka:** zadržati. Oznake u CSV-u: `PC2-20261109-M4-009-*`.

### `smoke_lab`: poređenje verzija jednog jezgra (od 2026-11-09)

`Solver/build/smoke_lab` pokazuje verzije jezgra jednu do druge, po uzoru na primjer iz laba
(`BenchmarkRunner::run()`, `measure()`, `check_vector`).
- Svaka verzija je zasebna funkcija i prvo se provjerava prema verziji 1, bit po bit.
- Zatim se mjeri u 3 naizmjenične runde, svaka sa 1 zagrijavanjem i 10 mjerenja. Jedno mjerenje je
  40 Jacobi iteracija.
- Ispisuje medijan, minimum, raspon rundi, S prema verziji 1 i poboljšanje prema prethodnoj verziji.
- Traje oko 14 s. Mjeri samo jezgro (bez `setBoundary`), pa je dopuna ovom dnevniku, a ne zamjena
  za mjerenje cijelog koraka.

### M4-010: T9 ručne AVX2 intrinsics za Jacobi: NE KORISTITI U SOLVERU (bez dobitka prema M4-009)

- **Izmjena:** `jacobiSlabAvx2Intrinsics` u `Projection_avx2.cpp` koristi `_mm256_loadu_ps`,
  `_mm256_add_ps`, `_mm256_div_ps`, a ostatak reda radi skalarno. Šest susjeda se sabira istim
  redoslijedom kao u skalarnom kodu, bez FMA, pa je rezultat bit po bit isti (dodano u test
  `jacobi_avx2_and_scalar_builds_are_bit_identical`).
- **Dostupnost:** `KernelIsa::Avx2Intrinsics`; `KernelIsa::Auto` ga **ne** bira.
- **Mjerenje (`smoke_lab`, PC-2, 3 runde, jezgro bez `setBoundary`):**

| Mreža | AVX2 po fajlu (M4-009) ms | AVX2 intrinsics ms | prema M4-009 | raspon rundi (M4-009 / intr.) |
| :--- | ---: | ---: | ---: | :--- |
| 32³ | 0.322 | 0.311 | +3.3% | 0.006 / 0.008 |
| 64³ | 2.566 | 2.563 | +0.1% | 0.519 / 0.117 |
| 96³ | 11.197 | 11.084 | +1.0% | 0.944 / 1.496 |
| 128³ | 42.596 | 42.464 | +0.3% | 2.895 / 2.948 |

- **Tumačenje:** g++ iz istog C++ koda (M4-009) već pravi jednako dobar AVX2 kod: 8 floatova,
  neporavnata učitavanja, unroll 8. Razlika je unutar raspona rundi, osim možda na 32³ (+3%).
- **Odluka:** solver ostaje na M4-009 (jednostavniji kod, jedan izvor za obje putanje). Intrinsics
  verzija ostaje samo za demonstraciju u `smoke_lab` (verzija 5) i za test tačnosti. Put kroz solver
  se nije promijenio, pa cijeli korak nije ponovo mjeren.
- Za poređenje, ukupno na Jacobi jezgru prema M3 kodu bez vektorizacije: **3.7x (32³), 3.9x (64³),
  2.9x (96³), 2.0x (128³)** (verzija 4 u `smoke_lab`).

### Profil kompajlera za advekciju na PC-2 (T7)

- Prije M4-011 unutrašnja petlja `advectScalar` nije vektorizirana: "unsupported control flow in
  loop". Uzrok je `clampToBox`, čiji ternarni operatori postaju skokovi.
- Poslije M4-011 grananja nema, ali ostaje prepreka "evolution of base is not affine", isto kao
  ranije kod gradijenta (aliasing kroz `Field3D`). Sljedeći korak je zato T12.
- Iza toga ostaje treća prepreka: trilinearna interpolacija čita sa adresa koje zavise od brzine
  (gather, T14 u labu).

### M4-011: T10 clamp bez grananja u advekciji: ZADRŽATI

- **Izmjena:** `clampToBox` (`Solver/src/stages/Interpolation.h`) je sada
  `std::min(std::max(x, lo), hi)` (`minss`/`maxss`), umjesto `x < lo ? lo : (x > hi ? hi : x)`.
  Rezultat je isti za svaki ulaz, uključujući NaN.
- **Tačnost:** 44/44 testa; `--verify` daje bit po bit isto.
- **Mjerenje:** osnova je `31ad2f8`, 3 runde.

| Mreža | Adv. brzine: osnova / nova (ms) | S | Adv. skalara: osnova / nova (ms) | S | Cijeli korak S | Presude (brzina / skalari / korak) |
| :--- | :--- | ---: | :--- | ---: | ---: | :--- |
| 32³ | 0.619 / 0.613 | 1.009x | 0.520 / 0.512 | 1.016x | 1.021x | brže / šum / brže |
| 64³ | 5.270 / 5.087 | 1.036x | 3.779 / 3.675 | 1.028x | 1.015x | brže / brže / šum |
| 96³ | 18.300 / 18.862 | 0.970x | 13.680 / 12.362 | 1.107x | 1.005x | sporije / šum / šum |
| 128³ | 42.266 / 41.195 | 1.026x | 28.948 / 28.228 | 1.026x | 1.008x | brže / brže / šum |

- **Tumačenje:** dobitak je mali (1 do 3.5%), jer petlja još nije vektorizirana; uklonjeno je samo
  grananje. Granice su rijetko dostignute, pa je grananje i inače bilo dobro predvidivo. 96³ je i
  ovdje nekonzistentna (vidi uvod odjeljka 6).
- **Odluka:** zadržati. Ovo je preduslov za vektorizaciju, uz mali dobitak i bez promjene rezultata.
  Oznake u CSV-u: `PC2-20261109-M4-011-*`.
