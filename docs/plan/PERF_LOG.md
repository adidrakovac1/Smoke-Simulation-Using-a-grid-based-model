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
referenca su u odjeljku 6. Unosi M4-001 do M4-005 (odjeljak 5) vrijede za PC-1 i AppleClang. Opseg M4 je skraćen
(odluka D11); zbir M4 prema B0/B1 je u odjeljku 7.

---

## 1. Otisci računara tima

Popuniti jednom po računaru, a promijeniti kad se promijeni nešto od ovoga (drajver, kompajler,
plan napajanja).

| Oznaka | Vlasnik | Procesor | Jezgara/niti | L2 / L3 | RAM | AVX2 | Kompajler | Napomena |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| PC-1 | Adi | Apple M4 | 10/10 | L2 4 MiB / L3 nije prijavljen | 16 GiB | AVX2 ne, ARM NEON da | AppleClang 21.0.0.21000101 | macOS 25.6.0 |
| PC-2 | Ermin | AMD Ryzen 5 5500 (Zen 3) | 6/12 | L2 6×512 KiB / L3 16 MiB | 32 GiB (2×16 DDR4, dvokanalno, 2400 MT/s) | AVX2 da, FMA da, AVX-512 ne | g++ 16.2.0 (MSYS2 UCRT64) | Windows 11 Pro 10.0.26300, plan napajanja High performance; puna tabela u PROFILE.md |
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
  računa** dok se ne provjeri. **Ispravljeno 2026-11-11 (odjeljak 8):** stari triad je bio 6 puta
  prevelik; ispravljena vrijednost je oko 17.9 GB/s, a teorijski maksimum ovog računara 38.4 GB/s
  (RAM radi na 2400 MT/s, ne na 3200).

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
- Zatim se mjeri u 3 naizmjenične runde, svaka sa 1 zagrijavanjem i 10 mjerenja.
- Jezgra (`--kernel jacobi|advekcija|sve`, zadano sve):
  - **Jacobi:** jedno mjerenje je 40 Jacobi iteracija; verzije M3 bez vektorizacije, M3 auto, M4-007,
    M4-009, M4-010.
  - **Advekcija** (dodano poslije M4-014): jedno mjerenje je 5 advekcija (u, v, w, gustina, temperatura),
    kao u jednom koraku. Verzije: M3 kod bez vektorizacije, M3 kod sa zadanim oznakama (g++ ga ne
    vektorizira, pa je isto kao verzija 1), vektorizirani SSE kod (M4-011 do M4-013) i AVX2 po fajlu
    (M4-014). M3 kod je sačuvan u `Solver/lab/AdvectionM3.inc`. Verzija 5 (od M4-017) je spojena
    advekcija (`advectFields`, 3 + 2 polja), kao u solveru.
  - **Demonstracione verzije (dodano uz `smoke_demo`)**, nisu dio solvera: ručno odmotan M3 Jacobi
    (4 ćelije po prolazu, bez vektorizacije; tehnika T3, `lab/JacobiM3.inc`) i M3 advekcija na AoS
    rasporedu (`struct` sa u, v, w, gustinom i temperaturom po ćeliji; `lab/advection_aos.cpp`) kao
    poređenje sa SoA rasporedom koji solver koristi od M0. Obje daju bit po bit isti rezultat. Brojevi iz
    kratkih proba nisu mjerenje po D10 i ovdje se ne upisuju.
- Ispisuje medijan, minimum, raspon rundi, S prema verziji 1 i poboljšanje prema verziji navedenoj u
  zagradi (obično prethodnoj; AoS se poredi sa M3 SoA kodom sa istim oznakama).
- `smoke_demo` (`Solver/demo`) koristi iste tablice i ispred njih mjeri cijelu simulaciju: Neoptimizirana
  verzija (M3 kod iz `73ceb9e`, bez vektorizacije), M3 sa zadanim oznakama i Optimizirana verzija (M4),
  uz provjeru da je gustina na kraju bit po bit ista.
- Mjeri samo jezgro (bez `setBoundary`), pa je dopuna ovom dnevniku, a ne zamjena za mjerenje cijelog
  koraka.

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

### M4-012: T12 `restrict` u advekciji: ZADRŽATI (preduslov, bez ubrzanja)

- **Izmjena:** unutrašnja petlja advekcije je funkcija po redu `advectRow` sa `SMOKE_RESTRICT`
  parametrima (`Solver/src/stages/AdvectionRows.inc`). Trilinearna interpolacija čita sirov pokazivač
  (`sampleTrilinear(const float*, sJ, sK, x, y, z)` u `Interpolation.h`), istim redom računa kao M1 oblik.
  `src` smije biti isti niz kao `u`, `v` ili `w` (`advectVelocity`), jer se oni samo čitaju.
- **Tačnost:** 45/45 testova (novi test: sirovi oblik interpolacije je bit po bit isti kao M1 oblik);
  `--verify` daje bit po bit isto.
- **Mjerenje:** osnova je `b860c02`; jedna zajednička sesija za M4-012, M4-013 i M4-014 (4 programa,
  3 rotirane runde).

| Mreža | Adv. brzine: osnova / nova (ms) | S | Adv. skalara: osnova / nova (ms) | S | Cijeli korak S | Presude (brzina / skalari / korak) |
| :--- | :--- | ---: | :--- | ---: | ---: | :--- |
| 32³ | 0.618 / 0.610 | 1.012x | 0.537 / 0.513 | 1.046x | 1.020x | šum / brže / šum |
| 64³ | 5.113 / 5.106 | 1.002x | 3.723 / 3.741 | 0.995x | 0.995x | šum / šum / šum |
| 96³ | 17.832 / 17.694 | 1.008x | 12.397 / 12.273 | 1.010x | 1.029x | šum / šum / šum |
| 128³ | 41.414 / 41.193 | 1.005x | 28.469 / 28.020 | 1.016x | 1.009x | šum / brže / šum |

- **Tumačenje:** kao i kod M4-002 na PC-1, `restrict` sam ne mijenja ništa: g++ i dalje ne vektorizira
  petlju ("data ref analysis failed"). Razlike su u šumu.
- **Odluka:** zadržati, jer je preduslov za M4-013. Oznake u CSV-u: `PC2-20261109-M4-012-*`.

### M4-013: vektorizacija advekcije (32-bitni indeks, red van linije): ZADRŽATI

- **Izmjena:** dvije stvari, i nijedna sama nije dovoljna (provjereno sa `-fopt-info-vec`):
  1. osam čitanja u `sampleTrilinear` koristi `int` indeks `c = i0 + sj*j0 + sk*k0` i `f[c + offset]`
     umjesto 64-bitne aritmetike pokazivača. Adrese su iste, pa je i rezultat isti. Uslov je manje od
     2^31 floatova po polju (`advectScalar` to provjerava sa `assert`).
  2. `advectRow` ostaje zasebna funkcija (`SMOKE_NOINLINE`, `Restrict.h`). Kad je g++ ugradi u petlju po
     redovima, `restrict` obećanja više ne pokrivaju čitanja koja zavise od brzine, pa odustane.
- **Šta kompajler napravi:** petlja je vektorizirana u zadanoj gradnji (SSE, 4 floata; 17 `mulps` u
  `Advection.cpp.obj`). Osam čitanja je "oponašani gather" (`pextrd`/`movss`/`insertps`), a clamp, težine
  i množenja su vektorski. `vgatherdps` g++ ne koristi.
- **Tačnost:** 45/45 testova; `--verify` daje bit po bit isto.
- **Mjerenje:** osnova je M4-012, ista sesija.

| Mreža | Adv. brzine: osnova / nova (ms) | S | Adv. skalara: osnova / nova (ms) | S | Cijeli korak S | Presude (brzina / skalari / korak) |
| :--- | :--- | ---: | :--- | ---: | ---: | :--- |
| 32³ | 0.610 / 0.465 | 1.314x | 0.513 / 0.373 | 1.373x | 1.186x | brže / brže / brže |
| 64³ | 5.106 / 3.660 | 1.395x | 3.741 / 2.638 | 1.418x | 1.250x | brže / brže / brže |
| 96³ | 17.694 / 12.296 | 1.439x | 12.273 / 8.640 | 1.421x | 1.205x | brže / brže / brže |
| 128³ | 41.193 / 28.796 | 1.431x | 28.020 / 19.652 | 1.426x | 1.179x | brže / brže / brže |

- **Tumačenje:** advekcija je 1.31 do 1.44x brža, na svim mrežama i u svakoj rundi. Iako su čitanja
  pojedinačna, račun koordinata, clampa i težina (oko 30 operacija po ćeliji) sada radi na 4 ćelije
  odjednom. Dobitak ne pada na 128³, pa advekcija nije ograničena memorijom kao Jacobi.
- **Odluka:** zadržati. Oznake u CSV-u: `PC2-20261109-M4-013-*`.

### M4-014: T15/T16 AVX2 po fajlu za advekciju, s provjerom procesora: ZADRŽATI

- **Izmjena:** isti kod (`AdvectionRows.inc`) se gradi još jednom sa `-mavx2` u
  `Solver/src/stages/Advection_avx2.cpp`; `advectScalar` bira tu verziju kad `avx2Supported()` vrati true
  (`KernelIsa`, kao kod Jacobija u M4-009). Pomoćne funkcije u `Interpolation.h` su `static inline`, a
  `std::min`/`std::max` su zamijenjeni sa `minOf`/`maxOf` (napisani isto kao u standardnoj biblioteci).
  Inače bi linker mogao cijelom programu dati AVX2 kopiju, i program bi se rušio bez AVX2.
- **Šta kompajler napravi:** 8 floatova odjednom (17 `vmulps` na `ymm` registrima). Čitanja su i dalje
  oponašani gather (`vpextrd`/`vmovss`/`vinsertps`), bez FMA.
- **Tačnost:** 46/46 testova (novi test: AVX2 i skalarna advekcija su bit po bit iste, i kad je `src`
  isti niz kao `u`); `--verify` daje bit po bit isto.
- **Mjerenje:** osnova je M4-013, ista sesija.

| Mreža | Adv. brzine: osnova / nova (ms) | S | Adv. skalara: osnova / nova (ms) | S | Cijeli korak S | Presude (brzina / skalari / korak) |
| :--- | :--- | ---: | :--- | ---: | ---: | :--- |
| 32³ | 0.465 / 0.323 | 1.441x | 0.373 / 0.280 | 1.332x | 1.180x | brže / brže / brže |
| 64³ | 3.660 / 2.365 | 1.548x | 2.638 / 1.767 | 1.493x | 1.224x | brže / brže / brže |
| 96³ | 12.296 / 7.972 | 1.542x | 8.640 / 5.779 | 1.495x | 1.133x | brže / brže / brže |
| 128³ | 28.796 / 18.576 | 1.550x | 19.652 / 12.844 | 1.530x | 1.162x | brže / brže / brže |

- **Ukupno M4-012 do M4-014** prema `b860c02`: advekcija brzine 1.91 do 2.24x, advekcija skalara 1.91 do
  2.22x, cijeli korak 1.38 do 1.52x (sve presude "brže"). Projekcija se nije mijenjala (šum).
- **Tumačenje:** 8 umjesto 4 floata daje još 1.33 do 1.55x, iako čitanja ostaju pojedinačna. Projekcija
  je sada najveći dio koraka na svim mrežama (na 128³ oko 50%).
- **Moguće dalje:** ručni `_mm256_i32gather_ps` (T9/T14) da se provjeri da li pravi gather pomaže na Zen 3;
  nije izmjereno.
- **Odluka:** zadržati. Oznake u CSV-u: `PC2-20261109-M4-014-*`.

### M4-015: fuzija jezgara, advekcija brzine u jednom prolazu (nivo 2, M4b): ZADRŽATI

- **Odluka o opsegu:** D11 (PLAN.md). Preostali mali koraci nivoa 1 se ne rade; od nivoa 2 uzima se samo
  fuzija u advekciji, jer ne zavisi od procesora i ne mijenja rezultat.
- **Hipoteza:** u, v i w se advektuju istom (starom) brzinom, pa sve tri komponente imaju isti trag unazad.
  Ako se trag, clamp, indeks ćelije i 6 težina računaju jednom po ćeliji (umjesto tri puta), a u, v, w
  čitaju jednom, advekcija brzine treba biti primjetno brža. Interpolacija (8 čitanja po polju) ostaje.
- **Izmjena:** nova funkcija `advectFields` (`Solver/src/stages/Stages.h`, `Advection.cpp`) za 1 do 3 polja,
  s redom `advectRow3` u `AdvectionRows.inc`; `advectVelocity` je poziva jednom. `advectScalar` je sada
  `advectFields` sa jednim poljem.
- **Tačnost:** svako polje koristi iste x, y, z i isti `sampleTrilinear`, pa je bit po bit isto. Novi test
  `fused_advection_is_bit_identical_to_separate_calls` (skalarna i AVX2 gradnja); 47/47; `--verify` bit po
  bit isto.
- **Mjerenje:** osnova je `dfa2107`; jedna zajednička sesija za B0, B1, `dfa2107`, M4-015, M4-016 i M4-017
  (6 programa, 3 rotirane runde).

| Mreža | Adv. brzine: osnova / nova (ms) | S | Adv. skalara: osnova / nova (ms) | S | Cijeli korak S | Presude (brzina / skalari / korak) |
| :--- | :--- | ---: | :--- | ---: | ---: | :--- |
| 32³ | 0.311 / 0.229 | 1.357x | 0.267 / 0.273 | 0.976x | 1.070x | brže / šum / brže |
| 64³ | 2.305 / 1.866 | 1.235x | 1.693 / 1.711 | 0.989x | 1.002x | brže / šum / šum |
| 96³ | 7.947 / 6.525 | 1.218x | 5.557 / 5.471 | 1.016x | 0.985x | brže / šum / šum |
| 128³ | 18.447 / 15.040 | 1.227x | 12.600 / 12.523 | 1.006x | 1.059x | brže / šum / brže |

- **Tumačenje:** advekcija brzine je 1.22 do 1.36x brža u svakoj rundi. Cijeli korak je ponegdje u šumu,
  jer projekcija na PC-2 jako varira (vidi uvod odjeljka 6).
- **Odluka:** zadržati. Oznake u CSV-u: `PC2-20261109-M4-015-*`.

### M4-016: fuzija jezgara, gustina i temperatura u jednom prolazu: ZADRŽATI

- **Izmjena:** `advectScalars` advektuje gustinu i temperaturu jednim pozivom `advectFields` (`advectRow2`);
  oba `setBoundary` idu poslije. Rezultat je bit po bit isti (isti test; 47/47; `--verify`).
- **Mjerenje:** osnova je M4-015, ista sesija.

| Mreža | Adv. brzine: osnova / nova (ms) | S | Adv. skalara: osnova / nova (ms) | S | Cijeli korak S | Presude (brzina / skalari / korak) |
| :--- | :--- | ---: | :--- | ---: | ---: | :--- |
| 32³ | 0.229 / 0.231 | 0.994x | 0.273 / 0.205 | 1.331x | 1.065x | šum / brže / brže |
| 64³ | 1.866 / 1.843 | 1.012x | 1.711 / 1.502 | 1.139x | 1.090x | šum / brže / šum |
| 96³ | 6.525 / 6.945 | 0.940x | 5.471 / 5.270 | 1.038x | 0.874x | šum / šum / šum |
| 128³ | 15.040 / 14.930 | 1.007x | 12.523 / 11.825 | 1.059x | 1.006x | šum / brže / šum |

- **Tumačenje:** advekcija skalara je 1.06 do 1.33x brža (96³ u šumu). Dobitak je manji nego kod brzine:
  dijele se samo 2 polja. Na AVX2 gradnji g++ je spojene petlje sam suzio na 4 floata (vidi M4-017).
- **Odluka:** zadržati. Oznake u CSV-u: `PC2-20261109-M4-016-*`.

### M4-017: `-fvect-cost-model=unlimited` samo za `Advection_avx2.cpp`: ZADRŽATI

- **Uzrok:** `-fopt-info-vec` pokazuje da g++ za `advectRow2`/`advectRow3` u AVX2 fajlu bira vektore od
  16 bajta (4 floata) zbog svoje procjene cijene (oponašani gather). Sa `unlimited` bira 32 bajta (8 floatova).
- **Izmjena:** opcija u `Solver/CMakeLists.txt`, samo za g++ i samo za taj fajl. Ista aritmetika, bit po bit
  isto (47/47; `--verify`). Na drugim kompajlerima i u gradnji bez AVX2 nema efekta.
- **Mjerenje:** osnova je M4-016, ista sesija.

| Mreža | Adv. brzine: osnova / nova (ms) | S | Adv. skalara: osnova / nova (ms) | S | Cijeli korak S | Presude (brzina / skalari / korak) |
| :--- | :--- | ---: | :--- | ---: | ---: | :--- |
| 32³ | 0.231 / 0.219 | 1.052x | 0.205 / 0.184 | 1.116x | 1.030x | brže / brže / brže |
| 64³ | 1.843 / 1.751 | 1.053x | 1.502 / 1.332 | 1.128x | 1.026x | brže / brže / brže |
| 96³ | 6.945 / 6.174 | 1.125x | 5.270 / 4.528 | 1.164x | 1.176x | brže / brže / šum |
| 128³ | 14.930 / 14.607 | 1.022x | 11.825 / 10.387 | 1.138x | 1.016x | šum / brže / šum |

- **Tumačenje:** procjena kompajlera ovdje nije bila tačna; 8 floatova je brže na svim mrežama.
- **Ukupno M4-015 do M4-017** prema `dfa2107`: advekcija brzine 1.26 do 1.42x, advekcija skalara 1.21 do
  1.45x, cijeli korak 1.08 do 1.17x (96³: šum).
- **Odluka:** zadržati. Oznake u CSV-u: `PC2-20261109-M4-017-*`. Verzija 5 u `smoke_lab --kernel advekcija`
  pokazuje spojenu advekciju.

### Profil kompajlera za sile na PC-2 (T7)

- Poslije M4-017 sile su u `smoke_demo` bile ubrzane samo oko 1.1x prema M3 i bile su oko 24% koraka na 64³.
  `-fopt-info-vec` za `Forces.cpp`: uzgon je vektoriziran, a **obje petlje vorticity confinementa nisu**.
- Uzroci: (1) `std::sqrt` bez `-fno-math-errno` ostavlja poziv biblioteke (zbog `errno`); (2) "evolution of
  base is not affine" (preklapanje kroz `Field3D`, kao ranije kod projekcije i advekcije); (3) "unsupported
  control flow": `?:` koji bira djelitelja (`|grad| > 0 ? |grad| : 1`) g++ ostavlja kao skok.

### M4-018: `-fno-math-errno` za `Forces.cpp`: ZADRŽATI (preduslov, bez ubrzanja)

- **Izmjena:** opcija u `Solver/CMakeLists.txt`, samo za `Forces.cpp` i `Forces_avx2.cpp`, samo g++/Clang.
  Poziva `sqrt` u objektu više nema. Rezultat se ne mijenja: `sqrt` nenegativnog broja je tačan po IEEE 754,
  a opcija samo izostavlja postavljanje `errno` za negativan ulaz. Nije dio opcija `-ffast-math` koje mijenjaju
  vrijednosti (odluka D12).
- **Tačnost:** 47/47; `--verify` bit po bit isto.
- **Mjerenje:** osnova je `e9c2fa4`; jedna zajednička sesija za B0, B1, `e9c2fa4`, M4-018, M4-019 i M4-020
  (6 programa, 3 rotirane runde).

| Mreža | Sile: osnova / nova (ms) | S | Cijeli korak S | Presude (sile / korak) |
| :--- | :--- | ---: | ---: | :--- |
| 32³ | 0.220 / 0.224 | 0.980x | 0.985x | sporije / sporije |
| 64³ | 1.888 / 1.904 | 0.992x | 1.003x | šum / šum |
| 96³ | 10.292 / 10.530 | 0.977x | 0.955x | šum / šum |
| 128³ | 17.445 / 17.023 | 1.025x | 1.018x | šum / šum |

- **Tumačenje:** sam za sebe ne pomaže (petlje i dalje nisu vektorizirane); na 32³ je 2% sporije.
- **Odluka:** zadržati kao preduslov za M4-019. Oznake u CSV-u: `PC2-20261109-M4-018-*`.

### M4-019: T12 `restrict` + maska bez skoka u vorticity confinementu: ZADRŽATI

- **Izmjena:** obje petlje su funkcije po redu sa `SMOKE_RESTRICT` (`Solver/src/stages/ForcesRows.inc`,
  `vorticityRow` i `confinementRow`), kao kod projekcije (M4-007). Djelitelj je napisan kao aritmetička maska
  `p*|grad| + (!p)*1` (`p = |grad| > 0`), što je tačno `|grad|` ili tačno 1, isto kao `?:` iz M4-003/M4-006.
  Oblik sa `static_cast<float>(p)` i `1 - p` g++ 16 opet ne vektorizira, pa je izabran ovaj.
- **Šta kompajler napravi:** obje petlje vektorizirane (SSE, 4 floata).
- **Tačnost:** novi test `vorticity_row_kernels_are_bit_identical_to_the_m4_006_form` (stari oblik sa
  `Field3D::at` je u testu kao referenca; uključuje dio s nultom brzinom, gdje je djelitelj 1); 48/48;
  `--verify` bit po bit isto.
- **Mjerenje:** osnova je M4-018, ista sesija.

| Mreža | Sile: osnova / nova (ms) | S | Cijeli korak S | Presude (sile / korak) |
| :--- | :--- | ---: | ---: | :--- |
| 32³ | 0.224 / 0.101 | 2.221x | 1.136x | brže / brže |
| 64³ | 1.904 / 0.855 | 2.226x | 1.146x | brže / brže |
| 96³ | 10.530 / 4.331 | 2.431x | 1.256x | brže / brže |
| 128³ | 17.023 / 10.136 | 1.679x | 1.090x | brže / brže |

- **Tumačenje:** sile su 1.7 do 2.4x brže u svakoj rundi, cijeli korak 1.09 do 1.26x. Na 128³ manje, jer
  dva prolaza kroz 7 polja postaju ograničena memorijom.
- **Odluka:** zadržati. Oznake u CSV-u: `PC2-20261109-M4-019-*`.

### M4-020: T15/T16 AVX2 po fajlu za vorticity confinement: ZADRŽATI

- **Izmjena:** isti `ForcesRows.inc` se gradi i sa `-mavx2` u `Solver/src/stages/Forces_avx2.cpp`;
  `applyVorticityConfinement` bira tu verziju kad `avx2Supported()` vrati true (sada ima `KernelIsa` i vraća
  `bool`, kao `jacobiIteration`). 8 floatova odjednom (`vsqrtps` na `ymm`), bez FMA.
- **Tačnost:** isti test za Scalar, Avx2 i Auto; 48/48; `--verify` bit po bit isto; gradnja bez AVX2 48/48 i
  bit po bit isto.
- **Mjerenje:** osnova je M4-019, ista sesija.

| Mreža | Sile: osnova / nova (ms) | S | Cijeli korak S | Presude (sile / korak) |
| :--- | :--- | ---: | ---: | :--- |
| 32³ | 0.101 / 0.073 | 1.381x | 1.030x | brže / brže |
| 64³ | 0.855 / 0.676 | 1.265x | 1.018x | brže / šum |
| 96³ | 4.331 / 3.715 | 1.166x | 1.073x | šum / brže |
| 128³ | 10.136 / 9.591 | 1.057x | 1.003x | brže / šum |

- **Ukupno M4-018 do M4-020** prema `e9c2fa4`: sile 1.82 do 3.01x, cijeli korak 1.11 do 1.29x (sve "brže").
- **Odluka:** zadržati. Oznake u CSV-u: `PC2-20261109-M4-020-*`.

---

## 7. Zbir M4 na PC-2 (2026-11-09)

Konačna verzija M4 (M4-020) prema osnovama B0 i B1 iz M3 (`73ceb9e`), ista sesija kao M4-018 do M4-020
(3 rotirane runde, 100 koraka, 10 zagrijavanja, 40 Jacobi iteracija). Ćelija: **S prema B0 / S prema B1**.
Rezultat je bit po bit isti kao PC-2 referenca (`serial_64_100steps_pc2.bin`), u AVX2 gradnji i u gradnji
bez AVX2 (`-DSMOKE_ENABLE_AVX2=OFF`, 48/48 testova).

| Faza | 32³ | 64³ | 96³ | 128³ |
| :--- | ---: | ---: | ---: | ---: |
| izvori | 1.81x / 1.36x | 1.59x / 1.36x | 1.67x / 1.57x | 1.10x / 1.04x |
| sile (uzgon, vorticity) | 3.91x / 3.68x | 3.64x / 3.47x | 4.70x / 3.00x | 2.24x / 2.15x |
| advekcija brzine | 2.80x / 2.81x | 2.95x / 2.96x | 3.11x / 2.92x | 2.89x / 2.88x |
| projekcija | 3.32x / 1.66x | 3.48x / 1.63x | 3.03x / 1.54x | 1.88x / 1.13x |
| advekcija skalara | 2.63x / 2.65x | 2.81x / 2.81x | 2.55x / 2.57x | 2.77x / 2.77x |
| **cijeli korak** | **3.08x / 2.34x** | **3.22x / 2.35x** | **3.24x / 2.33x** | **2.23x / 1.77x** |

Sve presude za cijeli korak i za sile su "brže" (D10). Prethodni zbir (poslije M4-017, sesija M4-015 do
M4-017) bio je 1.99 do 2.68x prema B0 i 1.60 do 2.02x prema B1; njegovi redovi u CSV-u ostaju
(`PC2-20261109-M4-SUM-*`).

**Šta je donijelo ubrzanje (po fazama):**
- **Advekcija (oko 2.8x):** clamp bez grananja (M4-011), `restrict` (M4-012), 32-bitni indeks i red van
  linije, pa je petlja vektorizirana (M4-013), AVX2 po fajlu (M4-014) i fuzija (M4-015 do M4-017).
  Sve osim M4-014 i M4-017 vrijedi i bez AVX2.
- **Projekcija (3.0 do 3.5x prema B0, 1.5 do 1.7x prema B1; na 128³ manje):** auto-vektorizacija iz M3 (B1),
  `restrict` (M4-007) i AVX2 po fajlu (M4-009). Na 128³ Jacobi je ograničen memorijom, pa SIMD daje manje.
- **Sile (2.2 do 4.7x):** vorticity confinement bez grananja (M4-003, M4-006), `-fno-math-errno` (M4-018),
  `restrict` i maska, pa su obje petlje vektorizirane (M4-019), AVX2 po fajlu (M4-020).

**Ograničenja:**
- Na 128³ projekcija je sada oko 59% koraka. Dalji dobitak traži manje prolaza kroz memoriju ili manje
  iteracija (vremensko blokiranje, red-black GS, multigrid), što D11 ostavlja za kasnije, jer je teže ili
  mijenja rezultat.
- Brojevi vrijede za PC-2. Na računaru bez AVX2 otpadaju doprinosi M4-009, M4-014, M4-017 i M4-020; ostalo
  ostaje. Na PC-1 (Mac, ARM) M4 nije ponovo mjeren.
- E (efikasnost memorije) nije izračunat: kalibracija na PC-2 daje nevjerovatnih ~107 GB/s (odjeljak 6), pa
  zaključci stoje samo na S i Δ. (Kalibracija je ispravljena 2026-11-11 i E je izračunat za Jacobi, odjeljak 8.)
- Oznake u CSV-u: `PC2-20261109-M4-SUM2-B0-*` i `PC2-20261109-M4-SUM2-B1-*`.

---

## 8. Kalibracija memorije i E na PC-2 (2026-11-11, M4d / P0)

Ovo **nije izmjena brzine solvera** (kod solvera je isti kao `c9dca43`), nego ispravka mjernog alata. Puno
objašnjenje, tabela hardvera i postupak su u [PROFILE.md](PROFILE.md), odjeljci 1 i 2.

- **Greška:** `measureMemoryTriad` je 6 puta izvršavao `a = b + s·c` s nepromijenjenim `b` i `c`. Kompajler je
  ostavio jedan prolaz (potvrđeno u asembleru), a program je računao bajte za 6, pa je rezultat bio 6 puta
  prevelik (88 do 107 GB/s).
- **Ispravka:** nizovi rotiraju uloge (svaki prolaz čita ono što je prethodni upisao), jezgro je u funkciji
  `triadPass` koja se ne ugrađuje. `objdump -d` pokazuje petlju sa 6 poziva.
- **RAM:** 2 × 16 GiB DDR4 dvokanalno na **2400 MT/s** (XMP nije uključen). Teorijski maksimum je
  2 × 8 B × 2400·10⁶ = **38.4 GB/s**, a ne 51 GB/s kako je ranije pretpostavljeno.
- **Triad poslije ispravke:** 17.13, 18.49, 17.89, 18.07, 16.96 GB/s u 5 pokretanja; medijan **17.9 GB/s**
  (2 čitanja + 1 upis). S "write allocate" (3 čitanja + 1 upis, model) 23.8 GB/s. Oba ispod 38.4 GB/s.
- **Tačnost:** 48/48 testova; `--verify` prema `serial_64_100steps_pc2.bin` bit po bit isto.

**E za Jacobi iteraciju** (jedno pokretanje `smoke_bench`, 100 + 10 koraka, K = 40, triad u istoj sesiji
18.29 GB/s; računar nije bio umiren, nije protokol D10):

| Mreža | Jacobi iter. medijan / min (ms) | GB/s uz 12 B po ćeliji | E | GB/s uz 32 B po ćeliji (stari model) |
| :--- | :--- | ---: | ---: | ---: |
| 32³ | 0.00966 / 0.00947 | 40.7 | 2.23 | 108.5 |
| 64³ | 0.0738 / 0.0711 | 42.6 | 2.33 | 113.7 |
| 96³ | 0.287 / 0.240 | 37.0 | 2.02 | 98.7 |
| 128³ | 1.465 / 1.262 | 17.2 | 0.94 | 45.8 |

- 12 B po ćeliji: čitanje starog pritiska, čitanje divergencije, upis novog pritiska (isti dogovor kao triad).
  Model od 32 B (VERIFICATION.md, odjeljak 4.4) broji i šest susjeda koji su već u kešu.
- **Zaključak:** na 128³ je E ≈ 0.94 (po minimumu 1.09), dakle Jacobi radi na granici propusnosti memorije ovog
  računara. Na 32³ do 96³ je E oko 2, jer su polja u kešu. Ovo je u skladu s nalazom da SIMD na 128³ daje malo
  (M4-009: 1.11x).
- Vremena u ovoj tabeli su viša nego u odjeljku 7 (npr. 128³: 1.47 prema 1.11 ms), jer je računar bio u
  upotrebi; ne koristiti ih kao osnovu za S.
- Oznake u CSV-u: `PC2-20261111-CAL-*`, presuda `info`.
