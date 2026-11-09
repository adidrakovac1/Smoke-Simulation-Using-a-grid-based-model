# PLAN: serijska faza simulacije dima

**Cilj ove faze:** ispravna, izmjerena i **sekvencijalno optimizirana** (jedna nit) 3D simulacija
dima u C++-u, odvojena od renderera, spremna da je kasnije paraleliziramo.
**Paralelizacija (OpenMP niti, GPU) nije dio ove faze i trenutno nije milestone** (odluka D8), ali se
kod piše tako da je ne otežava (ARCHITECTURE.md, odjeljak 5). SIMD vektorizacija na jednoj niti
(tehnike iz laboratorijske vježbe 1) **jeste** dio ove faze, u M4.

Trajanje: oko **6 sedmica** za M0 do M3, plus M4 (serijska optimizacija). Sedmice su procjena, ne
ugovor; tačne rokove predmeta unosimo kad ih potvrdimo.

Dokumenti: [ARCHITECTURE.md](ARCHITECTURE.md), [MATH.md](MATH.md), [VERIFICATION.md](VERIFICATION.md).

---

## 1. Podjela posla (troje)

Prema planskom dokumentu svi koriste isti interfejs `SmokeSolver::step(dt)`. Za serijsku fazu:

| Uloga | Odgovornost | Glavni folderi |
| :--- | :--- | :--- |
| **A: jezgro solvera** | polja, rubni uslovi, izvor, uzgon, advekcija | `Solver/src/stages/{Boundary,Sources,Forces,Advection}.cpp`, `Field3D` |
| **B: provjera i mjerenje** | testovi, dijagnostika, tajmeri, konzolni program, benchmark, referentni rezultat | `Solver/tests`, `Solver/bench`, `Solver/apps`, `Profiler.h` |
| **C: pritisak (i na kraju vizualizacija)** | projekcija (Jacobi); `Integration/` tek u zadnjem milestone-u M-VIZ | `Solver/src/stages/Projection.cpp`, kasnije `Integration/` |

**Članovi tima:** Ermin Hadžić, Ajdin Đinalić, Adi Drakovac.
**Raspodjela uloga A / B / C:** još nije dogovorena (upisati ovdje: A = ______, B = ______, C = ______).

**Radni tok u gitu.** Svako radi na svojoj grani i spaja se kroz pull request, bez direktnih
commitova na glavnu granu. Preporučena konvencija (ista kao u `Renderer/GITFLOW.md`, ali ovdje
je ne provodi nikakav alat): grane `feature/<naziv>`, `bugfix/<naziv>`, `docs/<naziv>`; poruke
`tip(opseg): opis`, npr. `feat(solver): add semi-Lagrangian advection`.

---

## 2. Milestone-ovi

Svaki milestone ima **kriterij završetka**: bez ispunjenog kriterija se ne prelazi dalje.

### M0: priprema (sedmica 0 do 1)
- [ ] Svi u timu uspješno pokreću Renderer na svom računaru (PDF uputstvo).
- [ ] Fork i repozitorij dogovoreni, grane i pravila upisana.
- [x] Odluke D1 do D7 (odjeljak 3) donesene i upisane (2026-11-09).
- [x] Skelet `Solver/`: CMake, prazni headeri iz ARCHITECTURE.md, `smoke_cli` koji se gradi i
      ispisuje poruku, `smoke_tests` s jednim testom koji prolazi (commit 16b5f8f).
- [x] Prelazak testova na doctest (odluka D5, doctest 2.4.12). Detalji za M0 do M2: [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md).

**Kriterij:** `cmake` konfiguriše i gradi `Solver/` na računaru svakog člana, a test prolazi.

### M1: osnova (sedmica 1 do 2)
- [x] `GridSize`, `Field3D` (ghost sloj, poravnanje, `at(i,j,k)`, `swap`), `SolverConfig`, `SmokeParams`.
- [x] `setBoundary` za skalare i za komponente brzine (uključujući ivice i uglove ghost sloja).
- [x] Izvor dima (`+= dt·rate` u kugli; P2).
- [x] Semi-Lagrangian advekcija skalara (trilinearna interpolacija) s ograničavanjem na `[0.5, n+0.5]`.
- [x] Testovi T1, T2, T4, T5, T6, T7 (tolerancije za T1 i T6 izmjerene 2026-11-09, upisane u testove).
- [x] `smoke_cli` pokreće korake i snima presjek gustoće kao `.pgm` (`--dump-slices`, `--every`); demo
      `--demo advect` ispisuje težište, masu i broj NaN. Puna dijagnostika (`diagnostics()`) je u M2 (P3).

**Kriterij:** kugla gustoće u polju konstantne brzine se pomjera ispravno (T1) i simetrično (T6),
bez NaN. Rezultat se vidi kao slika.

### M2: brzina i pritisak (sedmica 2 do 4)
- [x] Uzgon (`applyBuoyancy`), polje temperature (po odluci D1); parametri izabrani eksperimentalno (MATH.md, odjeljak 3).
- [x] Advekcija brzine.
- [x] Projekcija: divergencija, Jacobi (dvije kopije polja), korekcija brzine.
- [x] `step(dt)` slaže cijeli korak u redoslijedu iz ARCHITECTURE.md.
- [x] Testovi T3, T8, T9 (plus T5 za cijeli korak i dijagnostika na zahtjev, P3).
- [x] Prag za T3 izmjeren i upisan (VERIFICATION.md, odjeljak 1).

**Stanje kriterija (2026-11-09, 64³):** stub se diže i razlije ispod zatvorenog vrha, ali se **slabo uvija**
(nalaz, očekivan u IMPLEMENTATION_PLAN.md, odjeljak 5.4; rješava se vorticity confinementom u M3).
Divergencija poslije projekcije: 0.229, 0.154, 0.125 za K = 20, 40, 80 (100 koraka). 500 koraka bez NaN.

**Kriterij:** dim se diže od izvora kao stub i pritom se uvija; divergencija poslije projekcije
je izmjerljivo manja nego prije i opada s brojem iteracija; 500 koraka bez NaN.

### M3: ljepota i mjerenje (sedmica 4 do 5)
- [x] Vorticity confinement (opcionalno uključiv parametrom `ε`).
- [x] Parametri podešeni da dim izgleda uvjerljivo (zapisati vrijednosti).
- [x] `StepTimings` i `ScopedTimer` oko svake faze; mjeri se i prosjek po Jacobi iteraciji.
- [x] `smoke_bench`: CSV po fazi za 32³, 64³, 96³, 128³ (VERIFICATION.md, odjeljak 4).
- [x] Referentni rezultat snimljen (`Solver/reference/`).
- [x] Osnove **B0** (bez vektorizacije) i **B1** (zadani kompajler) izmjerene i upisane kao prvi unosi u
      `PERF_LOG.md` i `perf/results.csv`; `smoke_bench --calibrate` ispisuje otisak računara (OPTIMIZATION.md, odjeljak 5).
- [x] T10 izmjeren i upisan.

**Rezultat (2026-11-09, Apple M4, AppleClang 21.0.0, B1):** projekcija je najveća pojedinačna faza
na svim izmjerenim mrežama, s udjelom 37.45% (64³) do 41.89% (32³). Na 128³ iznosi 39.46%;
advekcija brzine je sljedeća s 30.05%. Serijska tabela i B0/B1 poređenje su u
[VERIFICATION.md](VERIFICATION.md), odjeljak 4, a puni CSV u `perf/results.csv`.

**Kriterij:** postoji tabela serijskih vremena po fazi i udio svake faze (procjena `f` za Amdahlov
zakon), i očekivano najveći udio ima projekcija (potvrditi mjerenjem; ako nije, to je nalaz,
ne greška).

### M4: serijska optimizacija (od sedmice 5; trajanje odrediti kad znamo rokove predmeta)
Sve u M4 je **sekvencijalno (jedna nit)**: bez OpenMP niti i bez GPU-a (odluka D8). Postupak, pravila
mjerenja i evidencija su u [OPTIMIZATION.md](OPTIMIZATION.md); svaka izmjena ide u [PERF_LOG.md](PERF_LOG.md)
i `perf/results.csv`. Redoslijed jezgara bira se po profilu iz M3 (najskuplja faza prva).

**M4a: tehnike iz laboratorijske vježbe 1** (OPTIMIZATION.md, odjeljak 2, nivo 1)
- [ ] Za svako bitno jezgro (Jacobi, divergencija/korekcija, advekcija, uzgon, redukcije) prođeni
      koraci 2 do 10 iz OPTIMIZATION.md, odjeljak 2.3: redoslijed petlji, `restrict`, izvještaj
      auto-vektorizacije, `omp simd` (`-fopenmp-simd`, bez niti), poravnanje, maske bez grananja,
      blokiranje, AVX2 s rezervnom putanjom, više akumulatora.
- [ ] Svaki korak: testovi i poređenje s referentnim rezultatom, mjerenje prema osnovi (S, Δ),
      unos u evidenciju, i kad nije pomoglo.

**M4b: dodatne sekvencijalne optimizacije** (izvan laba; OPTIMIZATION.md, odjeljak 3.1)
- [ ] Počinje tek kad je M4a iscrpljen (kriterij iz OPTIMIZATION.md, odjeljak 2.3) i uz odobrenje tima.
- [ ] Kandidati: fuzija jezgara, vremensko blokiranje Jacobija, red-black Gauss-Seidel,
      multigrid / konjugirani gradijent, FMA, manji tip podataka, AVX-512 (ako ga hardver ima),
      prefetch. Bira se po profilu; nije obaveza uraditi sve.

**M4c: zatvaranje faze**
- [ ] Pregled koda među članovima, očišćeni TODO-ovi.
- [ ] Nacrt izvještaja: opis algoritma, tačnost, tabela serijskih vremena **i** tabela ubrzanja
      (S, Δ, E) po tehnici, prema osnovama B0/B1.
- [ ] Sve odluke i izmjerene vrijednosti upisane u dokumente.
- [ ] Granica `SlabRange` i dalje poštovana (da kasnija paralelizacija ne traži prepravljanje).

**Kriterij:** za svako bitno jezgro nivo 1 je iscrpljen i upisan; dodatne tehnike koje su probane
su izmjerene i upisane (zadržane ili odbačene); optimizirana verzija prolazi sve testove i poređenje
s referentnim rezultatom; postoji tabela ubrzanja serijske verzije u odnosu na B0 i B1.

### Paralelizacija: nije milestone (odluka D8)
OpenMP niti, GPU i ostale paralelne tehnike (OPTIMIZATION.md, odjeljak 3.2) se **ne planiraju** u ovoj
fazi. Kad tim odluči da počinje, za to se pišu posebni milestone-ovi. Do tada se paralelni kod ne piše.

### M-VIZ: vizualizacija u rendereru (ZADNJI milestone ove faze)
Radi se tek kad je solver gotov, sekvencijalno optimiziran i izmjeren. Do tada se dim provjerava konzolnim programom
i slikama presjeka (`.pgm`, vidi [CLI_OUTPUT.md](CLI_OUTPUT.md)), bez renderera.
Razlog što može čekati: Solver je odvojen i renderer čita samo `density()` (ARCHITECTURE.md).
- [ ] `Integration/` s `SmokeScene` registrovanom u `test-app`.
- [ ] `Texture3D` i prenos gustoće svaki frejm (`glTexSubImage3D`).
- [ ] Volumni prolaz: ray marching kroz kocku, prozirnost (Beer-Lambert), crtanje u HDR FBO u
      `OnPostRender`. Reference: [Crane07].
- [ ] ImGui panel: izbor veličine mreže (32/64/96/128 + Apply), parametri, pauza, jedan korak, reset.

**Kriterij:** dim se vidi u realnom vremenu u rendereru, a veličina mreže se mijenja iz menija.

---

## 3. Odluke (donesene u M0)

Odluke D1 do D7 prihvaćene su **2026-11-09** onako kako su bile preporučene, osim D5, kod koje je
izabran način bez preuzimanja s interneta. D8 je dodana istog dana.

| ID | Pitanje | Odluka |
| :--- | :--- | :--- |
| D1 | Uvodimo li polje temperature (Fedkiw) ili samo gustoću? | Uvesti, uz mogućnost isključivanja (`β = 0`) |
| D2 | Ghost sloj ili provjere rubova u petljama? | Ghost sloj (kao [Stam03]) |
| D3 | Vrh kutije zatvoren ili otvoren? | Zatvoren u serijskoj fazi, otvoren kasnije kao opcija |
| D4 | Jedinice: ćelije (`h = 1`) ili fizička veličina? | Jedinice ćelije (jednostavnije), znajući da se parametri ne prenose automatski između veličina mreže |
| D5 | Alat za testove | doctest, **jedan header zapakovan u repozitorij** (`Solver/third_party/doctest/doctest.h`), bez `FetchContent`, da se Solver gradi bez interneta |
| D6 | Jezik komentara u kodu | Engleski (kao Renderer); objašnjenja u razgovoru i dokumentima na bosanskom |
| D7 | Jezik i kodiranje ispisa konzolnog programa ([CLI_OUTPUT.md](CLI_OUTPUT.md)) | Bosanski bez dijakritika (ASCII) |
| D8 | Opseg optimizacije u ovoj fazi | M4 je **samo sekvencijalan**: prvo tehnike iz laboratorijske vježbe 1 (uključujući SIMD na jednoj niti), pa dodatne sekvencijalne tehnike. OpenMP niti i GPU nisu milestone; planiraju se tek kad tim odluči |
| D9 | Konačni ε (vorticity confinement) i računar za M4 (2026-11-09) | ε = 5 ostaje zadano, uz poznatu veću preostalu divergenciju (MATH.md, odjeljak 3). M4 nastavlja Ermin na PC-2 (x86, AVX2); M4 se poredi samo s PC-2 osnovama i PC-2 referencom |
| D10 | Kada je izmjena brža od šuma (2026-11-09) | Najmanje 3 naizmjenične runde; šum = raspon medijana rundi osnove; izmjena prolazi ako je razlika veća od šuma i ako je brža u svakoj rundi (OPTIMIZATION.md, odjeljak 4, tačka 5). Zamjenjuje pravilo "veće od max − min svih koraka" |

---

## 4. Šta se NE radi u ovoj fazi

- OpenMP niti, GPU i bilo kakav višenitni kod (odluka D8). Nisu milestone.
- Optimizacija prije M4. U M1 do M3 pišemo jasan, ispravan kod; brzina dolazi tek u M4, poslije mjerenja.
- Izmjene u `Renderer/` osim dvije dozvoljene (ARCHITECTURE.md, odjeljak 6).
- Vizualizacija u rendereru (to je zadnji milestone, M-VIZ).
- Zaklanjanje dima objektima scene (zahtijeva izmjenu depth buffera u Rendereru).
- Optimizacije "na slijepo". Prvo mjerenje, pa tek onda izmjena.

---

## 5. Rizici

| Rizik | Posljedica | Odgovor |
| :--- | :--- | :--- |
| Renderer se ne gradi na računaru nekog člana | blokira M-VIZ (na kraju) za tu osobu | PDF uputstvo; Solver se gradi bez Renderera pa ostali ne čekaju |
| Dim izgleda loše (previše razmazan) | slab demo | vorticity confinement, podešavanje parametara; bolja interpolacija je kasnija opcija |
| Pogrešan znak ili indeks u projekciji | dim se čudno ponaša | testovi T3 i T6, vizuelna provjera, poređenje s [Stam03] |
| Rezultati mjerenja šume | nepouzdana tabela | protokol iz VERIFICATION.md (zagrijavanje, medijan, Release) |
| Izmjena u Rendereru koja zatreba kasnije (depth) | dodatni posao | izolovano u odjeljak 6 ARCHITECTURE.md i odlučuje se zajednički |
