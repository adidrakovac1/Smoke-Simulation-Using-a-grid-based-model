# OPTIMIZATION: plan ubrzavanja, redoslijed tehnika i pravila mjerenja

Ovaj dokument vrijedi za **cijeli rad na ubrzanju**, počevši od milestone-a **M4 (serijska optimizacija)**
u PLAN.md. U M4 je sve **sekvencijalno (jedna nit)**: nivo 1 i sekvencijalni dio nivoa 2 (odjeljak 3.1).
Paralelni dio (odjeljak 3.2) trenutno nije milestone (odluka D8). Dokument odgovara na tri pitanja:
1. **Šta prvo isprobavamo?** Sve što smo već radili u laboratorijskoj vježbi o vektorizaciji (nivo 1, M4a).
2. **Šta tek poslije?** Dodatne sekvencijalne tehnike (nivo 2, odjeljak 3.1, M4b), tek kad se nivo 1 iscrpi.
3. **Kako se mjeri i poredi** tako da brojevi ne ovise o tome čiji je računar jači, i kako se vodi evidencija.

Povezano: [VERIFICATION.md](VERIFICATION.md) (tačnost i protokol mjerenja), [PERF_LOG.md](PERF_LOG.md)
(evidencija), [ARCHITECTURE.md](ARCHITECTURE.md) (pravila koda), [MATH.md](MATH.md) (Amdahl).

Referentni kod iz laba je u `docs/reference/PRS_Vektorizacija/` (nije naš kod, služi kao uzor).

---

## 1. Zlatna pravila

1. **Prvo tačnost, tek onda brzina.** Svaka izmjena mora proći testove i poređenje s referentnim
   rezultatom (VERIFICATION.md) prije nego što se mjeri.
2. **Mjeri prije nego što optimiziraš.** Optimizira se faza koja troši najviše vremena (profil iz M3).
   Amdahlov zakon: ubrzanje manje faze ne može dati mnogo.
3. **Jedna izmjena po koraku.** Inače ne znamo šta je pomoglo.
4. **Svaki korak se poredi s prethodnim stanjem i upisuje u evidenciju** (odjeljak 5 i PERF_LOG.md),
   i kad je rezultat loš (da ne ponavljamo istu grešku).
5. **Bez evidencije nema optimizacije.** Izmjena bez unosa u PERF_LOG.md se ne spaja u glavnu granu.
6. **Ne vjerovati pretpostavkama.** "Trebalo bi biti brže" ne vrijedi. Brojevi odlučuju, uključujući
   odluku da se izmjena **odbaci** ako ne pomaže ili je unutar šuma mjerenja.

---

## 2. Nivo 1: ono što smo već radili u labu (iscrpiti prvo)

Laboratorijska vježba (`lab1_perf_simd`) obrađuje sljedeće tehnike. Ovo je naša **početna
lista**; svaku primjenjujemo na jezgra solvera (advekcija, Jacobi, divergencija, uzgon, sumiranje)
i mjerimo.

### 2.1 Šta lab sadrži (inventar)

| # | Tehnika | Gdje u labu | Šta pokazuje |
| :-- | :--- | :--- | :--- |
| T1 | **Pouzdano mjerenje** | `bench.hpp` (`measure`, `consume`, `checksum`, `PERF_NOINLINE`) | zagrijavanje, ponavljanja, medijan i minimum; rezultat se "potroši" da kompajler ne izbaci posao |
| T2 | **Skalarna referenca bez vektorizacije** | `*_scalar.cpp`, `-fno-tree-vectorize`, `PERF_NO_VECTORIZE` | tačna osnova za računanje ubrzanja |
| T3 | **ILP: zavisni naspram nezavisnih lanaca** | `measurement_kernels.hpp` (`dependent`, `independent` s 4 lanca) | procesor izvršava nezavisne operacije istovremeno |
| T4 | **Redoslijed pristupa memoriji** | `memory_kernels.hpp` (`traverse` red/kolona) | prolazak po redovima je mnogo bolji za keš |
| T5 | **Redoslijed petlji** | `matmul` (ijk, ikj, jik, jki, kij, kji) | najbolja lokalnost ako je unutrašnja petlja sekvencijalna (npr. ikj) |
| T6 | **Blokiranje / tiling** | `matmul_tiled` (B = 8, 16, 32, 64) | rad na blokovima koji staju u keš |
| T7 | **Auto-vektorizacija** | `*_auto.cpp`, `PERFLABS_VECTORIZATION_REPORTS` (`-fopt-info-vec-*`, `-Rpass=loop-vectorize`) | kompajler sam vektorizira; izvještaji kažu zašto nije |
| T8 | **`#pragma omp simd`** i **`reduction`** | `*_omp.cpp` | tražimo vektorizaciju bez prave niti; `reduction(+:sum)` dozvoljava reorganizaciju sume |
| T9 | **AVX2 intrinsics** | `*_avx2.cpp` (`_mm256_*`) | ručna vektorizacija 8 floatova odjednom; ostatak (`n % 8`) u skalarnoj petlji |
| T10 | **Maske umjesto grananja** | `threshold_avx2` (`cmp` + `blendv`), `step_soa_branchless` | `if` u petlji koči vektorizaciju; zamjenjuje se maskom ili `min`/`max` |
| T11 | **Poravnanje memorije** | `AlignedFloats` (32 B), `saxpy_avx2_aligned` | poravnati naspram neporavnatih učitavanja |
| T12 | **`restrict` (nema aliasinga)** | `fn_may_alias` / `fn_no_alias`, `PERF_RESTRICT` | obećanje kompajleru da se nizovi ne preklapaju |
| T13 | **AoS naspram SoA** | `particle_update.hpp` | niz struktura naspram posebnih nizova; SoA se vektorizira |
| T14 | **Šta se ne vektorizira** | `prefix_scan` (zavisnost među iteracijama), `escape_*` (različit broj iteracija po elementu) | poznaj granice |
| T15 | **Detekcija procesora u toku rada** | `perf::avx2_supported()` (`__builtin_cpu_supports`) | AVX2 putanja se bira tek ako procesor to podržava, uz rezervnu putanju |
| T16 | **AVX2 po fajlu, ne globalno** | CMake: `-mavx2` samo za `*_avx2.cpp` | ostatak programa radi i na procesoru bez AVX2 |
| T17 | **Provjera tačnosti uz mjerenje** | `check_vector` (relativna tolerancija 1e-4), `verify`, poređenje sa `double` referencom | brza verzija se mjeri tek kad je tačna |

### 2.2 Kako se to preslikava na naša jezgra

| Naše jezgro | Karakter | Šta primijeniti (nivo 1) |
| :--- | :--- | :--- |
| **Jacobi iteracija** | 6 susjeda + divergencija, ograničen memorijom | T4/T5 (`i` je unutrašnja petlja), T7/T8 (`omp simd`), T9 (AVX2: `i±1` su neporavnata učitavanja), T11 (poravnanje redova), T12 (`restrict`), T6 (blokiranje po j/k ravninama) |
| **Divergencija, gradijent, korekcija brzine** | isto, jednostavna aritmetika | T7, T8, T9, T12 |
| **Uzgon (buoyancy)** | čisto elementwise, kao SAXPY | T7, T8, T9 |
| **Advekcija (semi-Lagrangian)** | čitanja s **adresa koje ovise o podacima** (trilinearna interpolacija), slično `escape` | T10 (clamp bez `if`), T12; auto-vektorizacija će vjerovatno pasti (T14); AVX2 *gather* je kandidat, ali ga **mjerimo** prije nego što povjerujemo da isplati |
| **Sume i maksimumi** (`Σρ`, `max|div|`, `max|u|`) | redukcije | T8 (`reduction`), T3 (više akumulatora), uz tolerancije jer se redoslijed sabiranja mijenja |
| **Rubni uslovi** | malo posla, mnogo grananja | T10; obično nebitno za profil |

Napomene za naš kod koje proizlaze iz laba:

- **Zbog poravnanja (T11) već sada dizajnirati korak redova ("pitch").** Ako je širina reda
  `nx + 2 = 66` floatova, red 1 ne počinje na granici od 32 bajta. Zato širinu reda zaokružiti na višekratnik
  od 8 (kasnije 16 za AVX-512). `DensityView::strideJ/strideK` i `Field3D` to već podržavaju (skelet ima padding na 8 floatova). Ovo je jeftino
  uraditi na početku, a skupo kasnije (ARCHITECTURE.md, odjeljak 3.2).
- **Gradnja mora biti Release.** `CMakeLists.txt` iz laba **ne postavlja** nivo optimizacije, pa bez
  `-DCMAKE_BUILD_TYPE=Release` brojevi ne vrijede ništa. Naš `Solver/CMakeLists.txt` mora sam
  postaviti Release kao zadano.
- **Vektorizacija po fajlu.** `-mavx2` samo na fajlovima s AVX2 putanjom i provjera procesora u toku rada (T15, T16),
  umjesto `-march=native` za sve. To je isto pravilo kao u ARCHITECTURE.md, odjeljak 5.
- **Komanda za OpenMP `simd` bez niti:** za `#pragma omp simd` na GCC-u dovoljna je opcija
  `-fopenmp-simd` (ne treba pravi OpenMP s nitima, koji u ovoj fazi i ne koristimo, odluka D8). *Provjeriti na našem
  kompajleru pri prvom korištenju.*
- **`PERF_NO_VECTORIZE`** za GCC koristi `#pragma GCC novector`, koji traži GCC 14 ili noviji (mi imamo 16.2).
- **Redukcije mijenjaju zaokruživanje.** Zbir u 8 traka ne daje isti bit-rezultat kao serijski zbir
  (lab to rješava tolerancijom). Naš odgovor: testovi s tolerancijom, a ne `==`, tamo gdje je to opravdano.
- **Jedna slabost laba koju ne prepisujemo:** `dot_avx2` ima jedan akumulator (lanac zavisnosti), pa je
  ograničen latencijom. To je mjesto za T3 (nekoliko akumulatora) i FMA u nivou 2.
- **Lab nema rezultata mjerenja u arhivi.** Ne tvrdimo ništa o ubrzanjima iz laba; svoje brojeve pravimo sami.

### 2.3 Redoslijed rada na jednom jezgru (primjer: Jacobi)

Za svako jezgro idemo istim redom, i svaki korak je zasebna izmjena sa zasebnim unosom u evidenciju:

| Korak | Varijanta | Pitanje na koje odgovara |
| :--- | :--- | :--- |
| 0 | **B0: skalarno, vektorizacija isključena** (`-O2 -fno-tree-vectorize`) | osnova za računanje ubrzanja |
| 1 | **B1: serijski, zadane optimizacije kompajlera** (`-O2`/`-O3`) | koliko kompajler sam već uradi |
| 2 | Redoslijed petlji i pristup memoriji (T4, T5) | je li `i` unutrašnja petlja svugdje |
| 3 | `restrict` (T12) | pomaže li kompajleru |
| 4 | Izvještaj vektorizacije (T7): zašto petlja nije vektorizirana, pa popravak | šta koči |
| 5 | `omp simd` (T8) | hoće li kompajler vektorizirati uz uputu |
| 6 | Poravnanje i padding redova (T11) | razlika aligned/unaligned |
| 7 | Bez grananja (T10) | koliko `if` košta |
| 8 | Blokiranje (T6), veličine blokova isprobati | pomaže li keš |
| 9 | AVX2 intrinsics s rezervnom putanjom (T9, T15, T16) | ručno naspram automatskog |
| 10 | ILP / više akumulatora za redukcije (T3) | za sume i maksimume |

**Nivo 1 je iscrpljen** kad su svi koraci koji imaju smisla za dato jezgro isprobani i upisani, a
sljedeća tehnika ne daje poboljšanje veće od šuma mjerenja. Tek tada se prelazi na nivo 2.

---

## 3. Nivo 2: naprednija ubrzanja (tek nakon nivoa 1)

Svaka stavka je **kandidat**, ne obaveza. Redoslijed biramo prema profilu i ograničenju
(računanje ili memorija).

### 3.1 Sekvencijalne tehnike (jedna nit): dio M4b

| Tehnika | Ideja | Napomena |
| :--- | :--- | :--- |
| **Fuzija jezgara** | spojiti faze koje prolaze kroz isti niz (npr. divergencija s prvom Jacobi iteracijom, uzgon s advekcijom) | smanjuje promet memorije, što je glavno usko grlo |
| **Vremensko blokiranje Jacobija** | više iteracija na jednom bloku dok je u kešu | rezultat može ostati isti kao kod običnog Jacobija ako se ivice blokova pravilno obrade; složeno za pisanje i provjeru |
| **Red-black Gauss-Seidel** | brža konvergencija, manje iteracija za isti kvalitet | mijenja rezultate (drugačiji algoritam), pa se poredi po kvalitetu, ne bit po bit |
| **Multigrid / konjugirani gradijent** | puno manje iteracija na velikim mrežama | napredno; veliki dobitak za 128³ |
| **FMA instrukcije** | množenje i sabiranje u jednoj instrukciji | mijenja zaokruživanje; opet tolerancije |
| **Manji tip podataka** | npr. skladištenje u 16 bita za polja koja nisu kritična | mijenja tačnost; ide uz mjerenje kvaliteta |
| **AVX-512** | 16 floatova odjednom | samo ako hardver postoji; provjera u toku rada |
| **Prefetch, huge pages** | fino podešavanje memorije | samo uz dokaz iz mjerenja da je memorija usko grlo |

### 3.2 Paralelne tehnike: NISU milestone (odluka D8)

Navedene su samo da se zna kuda projekat ide. U ovoj fazi se **ne pišu**; milestone-ovi za njih se prave tek
kad tim odluči da počinje paralelizacija.

| Tehnika | Ideja | Napomena |
| :--- | :--- | :--- |
| **OpenMP niti po slojevima** | podjela po `k` (`SlabRange`), `#pragma omp parallel for` | glavna tema predmeta; mjeriti na 1, 2, 4, ... niti; paziti na prvi dodir memorije (first touch) i na broj fizičkih jezgara |
| **NUMA** | raspored memorije po čvorovima | ima smisla tek uz više niti |
| **GPU (compute shaderi ili CUDA)** | polja kao 3D teksture na GPU, bez prenosa na CPU | posljednja faza planskog dokumenta |

Za svaku stavku prije početka upiši u PERF_LOG.md **hipotezu** ("očekujem X jer Y"), a poslije nje rezultat.

---

## 4. Protokol mjerenja (da poređenje bude pošteno)

Proširuje VERIFICATION.md, odjeljak 4. Za svako mjerenje:

1. **Release gradnja**, jedna ista konfiguracija za sve varijante koje se porede.
2. **Isti računar, ista sesija, naizmjenično.** Osnova (B0 ili prethodno stanje) i nova varijanta se
   mjere u istom pokretanju, jedna za drugom, više puta. Tako sporedni uticaji (zagrijavanje,
   zauzetost procesora, toplotno usporavanje) pogađaju obje isto.
3. **Zagrijavanje + ponavljanja:** najmanje 1 zagrijavanje i 10 mjerenja (kao u labu); prijavi **medijan**,
   **minimum** i **raspon** (max − min). Medijan je glavni broj.
4. **Provjera tačnosti prije mjerenja** (VERIFICATION.md). Netačna varijanta se ne mjeri.
5. **Šum (odluka D10, 2026-11-09):** osnova i kandidat se mjere u **najmanje 3 naizmjenične runde**
   (`perf/run_rounds.sh`, redoslijed se rotira svake runde). Šum je **raspon medijana rundi osnove**
   (najveći minus najmanji medijan runde). Izmjena se priznaje kao brža (ili sporija) samo ako je
   (a) razlika medijana veća od tog šuma **i** (b) kandidat je brži (sporiji) u **svakoj** rundi.
   Inače je presuda "šum". Računa to `perf/summarize_rounds.sh`.
   *Zašto ne max − min svih koraka:* na Windowsu pojedinačni koraci imaju zastoje sistema, pa je
   taj raspon ogroman. Na PC-2 je po njemu čak i 2x ubrzanje projekcije (B1 naspram B0, 32³)
   ispalo "unutar šuma" (raspon 1.07 ms, razlika 0.67 ms). Medijan po rundi je stabilan
   (raspon rundi 0.007 ms), pa mjeri stvarnu ponovljivost.
6. **Uslovi računara:** priključen na struju, plan napajanja na visoke performanse, zatvoreni teški
   programi (pretraživač, igre, sinhronizacija OneDrive), laptop ne smije biti pregrijan.
7. **Zapiši otisak računara** uz svaki unos (odjeljak 5.2).

---

## 5. Poređenje koje ne ovisi o snazi računara ("delta")

**Iskreno ograničenje:** apsolutno vrijeme izvršenja je uvijek različito na slabijem i jačem računaru
i ne može se učiniti "istim". Ono što **može** biti uporedivo među računarima su **relativni brojevi**.
Zato izvještavamo ove metrike, tim redoslijedom važnosti:

### 5.1 Metrike

| Metrika | Formula | Šta znači | Vrijedi među računarima? |
| :--- | :--- | :--- | :--- |
| **Ubrzanje S** | `S = T_osnova / T_varijanta` | koliko puta brže | **Da** (u istoj sesiji, na istom računaru) |
| **Delta Δ** | `Δ = (T_osnova − T_varijanta) / T_osnova · 100%` | koliko posto vremena je uštedjeno | **Da** (ista osnova kao za S) |
| **Efikasnost memorije E** | `E = postignuta GB/s / izmjerena GB/s računara` | koji dio memorijske propusnosti koristimo | Uglavnom da: mjeri se u odnosu na sam računar |
| **Normalizirano vrijeme** | `T_norm = T_varijanta / T_kalibracija` | vrijeme izraženo u "jedinicama računara" | Približno (kalibracija mora mjeriti isto usko grlo) |

- **S i Δ su glavne metrike.** Osnova se mjeri na istom računaru u istoj sesiji, pa jačina računara "pada
  van" iz odnosa. Ovo je "delta" koju tražimo.
- **Zašto E:** naš solver je uglavnom ograničen memorijom (jedan Jacobi prolaz čita oko 7 i piše 1 vrijednost
  po ćeliji). Zato kalibrišemo **propusnost memorije** računara (jednostavan test tipa SAXPY ili "triad" na nizu
  mnogo većem od keša), pa mjerimo koliki dio te propusnosti naše jezgro dostiže.
- **`T_kalibracija`:** vrijeme fiksnog kalibracionog jezgra na tom računaru, mjereno na isti način.
  Preporuka je da kalibracija ima dvije komponente: (a) memorijsku (propusnost, GB/s) za jezgra ograničena
  memorijom, (b) računsku (npr. `dependent`/`independent` lanci iz laba, ns po operaciji) za
  jezgra ograničena računanjem.
- **Zašto ne bi trebalo vjerovati bilo kakvim poređenjima samo po apsolutnim vremenima:** veličina keša, broj
  jezgara i memorijski podsistem utiču na to koja je optimizacija korisna. Npr. mreža 64³ može stati u keš
  jednog računara, a ne drugog, pa se **ubrzanje** razlikuje. Zato svaki unos nosi otisak računara i poredimo
  S i Δ **po računaru**, a zajednički zaključak pišemo kao raspon (min, medijan, max) preko računara tima.

### 5.2 Otisak računara (upisuje se uz svaki unos)

| Polje | Kako doći do toga |
| :--- | :--- |
| Procesor, broj jezgara i niti, keš L2 i L3 | PowerShell: `Get-CimInstance Win32_Processor \| Select Name,NumberOfCores,NumberOfLogicalProcessors,L2CacheSize,L3CacheSize` |
| RAM | Task Manager, tab Performance (ili `Get-CimInstance Win32_ComputerSystem`) |
| AVX2 / OpenMP dostupnost | ispis iz benchmark programa (kao `AVX2: yes` u labu) |
| Kompajler i oznake | `g++ --version` i tačne opcije iz `CMakeCache.txt` |
| Plan napajanja i napajanje | priključen / baterija, plan napajanja |
| Commit | `git rev-parse --short HEAD` |

### 5.3 Kalibracioni mod u benchmarku

`smoke_bench` treba imati mod `--calibrate` koji ispiše otisak računara i izmjeri kalibracione
vrijednosti (propusnost memorije, ns po zavisnoj operaciji). Kalibracija se pokreće u istoj sesiji kao
i mjerenje i upisuje u unos. `smoke_bench --calibrate` ispisuje CPU, OS, broj hardverskih niti,
RAM, dostupne podatke o cacheu, vektorsku ISA, kompajler, Release oznake, varijantu i commit; zatim
mjeri triad memorije i zavisni lanac. Mjerenje triadom je približno i zavisi od računara/opterećenja.
Triad je ispravljen 2026-11-11 (ranije je bio 6 puta prevelik; PROFILE.md, odjeljak 2). Od tada postoji i
`smoke_bench --hwinfo`: procesor, keševi i skupovi instrukcija pročitani instrukcijom CPUID, te kompajler i
opcije gradnje (uključujući opcije po fajlu), bez mjerenja.

Osnovne gradnje M3:

```sh
cmake -S Solver -B Solver/build-b1 -DCMAKE_BUILD_TYPE=Release
cmake -S Solver -B Solver/build-b0 -DCMAKE_BUILD_TYPE=Release -DSMOKE_DISABLE_VECTORIZATION=ON
```

`B0` isključuje kompajlersku vektorizaciju na GCC-u/Clang-u; `B1` je zadana optimizirana gradnja.
Na Windowsu gdje CMake generator koristi MSVC, B0 opcija trenutno nije podržana.

---

## 6. Evidencija

- **Čitljiva:** [PERF_LOG.md](PERF_LOG.md). Jedan unos po izmjeni, s tabelom i zaključkom (zadržati ili odbaciti).
- **Strojno čitljiva:** `perf/results.csv` (jedna linija po varijanti, po mjerenju). Iz nje prave se tabele i grafici za izvještaj.
- Obje se **ažuriraju u istom commitu** kao i izmjena koda.
- Odbačene izmjene se **također upisuju** ("nije pomoglo jer ..."), da ih niko ne ponovi.

### Svaki unos sadrži
Identifikator, datum, autor, commit, nivo i tehnika (T-broj ili nivo 2), jezgro/faza, hipoteza,
parametri (veličina mreže, broj Jacobi iteracija, broj niti, zastavice kompajlera), otisak računara,
`T_osnova` i `T_varijanta` (medijan, minimum, raspon), S, Δ, GB/s i E, rezultat provjere tačnosti
(najveća razlika prema referentnom rezultatu), zaključak.

---

## 7. Kako to koristi Claude CLI

Pravilo u `CLAUDE.md`: **svaka izmjena koja ima za cilj brzinu** mora (1) proći testove tačnosti,
(2) biti izmjerena prema osnovi u istoj sesiji, (3) biti upisana u PERF_LOG.md i `perf/results.csv`,
(4) navesti tehniku iz ovog dokumenta (T-broj ili nivo 2), (5) tražiti odobrenje prije prelaska na nivo 2,
(6) ostati sekvencijalna (bez OpenMP niti i GPU-a) dok tim ne odluči drugačije (odluka D8).
