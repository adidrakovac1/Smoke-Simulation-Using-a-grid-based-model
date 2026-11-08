# PLAN: serijska faza simulacije dima

**Cilj ove faze:** ispravna, izmjerena **serijska** (jedna nit) 3D simulacija dima u C++-u,
odvojena od renderera, spremna da je kasnije paraleliziramo. **Paralelizacija (OpenMP, SIMD,
GPU) nije dio ove faze**, ali se kod piše tako da je ne otežava (ARCHITECTURE.md, odjeljak 5).

Trajanje: oko **6 sedmica** (do početka paralelizacije). Sedmice su procjena, ne ugovor; tačne
rokove predmeta unosimo kad ih potvrdimo.

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
- [ ] Odluke D1 do D7 (odjeljak 3) donesene i upisane.
- [ ] Skelet `Solver/`: CMake, prazni headeri iz ARCHITECTURE.md, `smoke_cli` koji se gradi i
      ispisuje poruku, `smoke_tests` s jednim testom koji prolazi.

**Kriterij:** `cmake` konfiguriše i gradi `Solver/` na računaru svakog člana, a test prolazi.

### M1: osnova (sedmica 1 do 2)
- [ ] `GridSize`, `Field3D` (ghost sloj, poravnanje, `at(i,j,k)`), `SolverConfig`, `SmokeParams`.
- [ ] `setBoundary` za skalare i za komponente brzine.
- [ ] Izvor dima.
- [ ] Semi-Lagrangian advekcija skalara (trilinearna interpolacija) s ograničavanjem unutar kutije.
- [ ] Testovi T1, T2, T4, T5, T6, T7.
- [ ] `smoke_cli` pokreće korake, ispisuje dijagnostiku i snima presjek gustoće kao `.pgm`.

**Kriterij:** kugla gustoće u polju konstantne brzine se pomjera ispravno (T1) i simetrično (T6),
bez NaN. Rezultat se vidi kao slika.

### M2: brzina i pritisak (sedmica 2 do 4)
- [ ] Uzgon (`applyBuoyancy`), polje temperature (po odluci D1).
- [ ] Advekcija brzine.
- [ ] Projekcija: divergencija, Jacobi (dvije kopije polja), korekcija brzine.
- [ ] `step(dt)` slaže cijeli korak u redoslijedu iz ARCHITECTURE.md.
- [ ] Testovi T3, T8, T9.
- [ ] Prag za T3 izmjeren i upisan.

**Kriterij:** dim se diže od izvora kao stub i pritom se uvija; divergencija poslije projekcije
je izmjerljivo manja nego prije i opada s brojem iteracija; 500 koraka bez NaN.

### M3: ljepota i mjerenje (sedmica 4 do 5)
- [ ] Vorticity confinement (opcionalno uključiv parametrom `ε`).
- [ ] Parametri podešeni da dim izgleda uvjerljivo (zapisati vrijednosti).
- [ ] `StepTimings` i `ScopedTimer` oko svake faze.
- [ ] `smoke_bench`: CSV po fazi za 32³, 64³, 96³, 128³ (VERIFICATION.md, odjeljak 4).
- [ ] Referentni rezultat snimljen (`Solver/reference/`).
- [ ] Osnove **B0** (bez vektorizacije) i **B1** (zadani kompajler) izmjerene i upisane kao prvi unosi u
      `PERF_LOG.md` i `perf/results.csv`; `smoke_bench --calibrate` ispisuje otisak računara (OPTIMIZATION.md, odjeljak 5).
- [ ] T10 izmjeren i upisan.

**Kriterij:** postoji tabela serijskih vremena po fazi i udio svake faze (procjena `f` za Amdahlov
zakon), i očekivano najveći udio ima projekcija (potvrditi mjerenjem; ako nije, to je nalaz,
ne greška).

### M4: završetak serijske faze (sedmica 5 do 6)
- [ ] Pregled koda među članovima, očišćeni TODO-ovi.
- [ ] Nacrt dijela izvještaja za serijsku fazu: opis algoritma, tačnost, tabela vremena.
- [ ] Sve odluke i izmjerene vrijednosti upisane u dokumente.
- [ ] Priprema za paralelizaciju: lista petlji kandidata (iz mjerenja), granica `SlabRange` provjerena.

**Kriterij:** tim može krenuti u optimizaciju i OpenMP bez prepravljanja arhitekture.

### Poslije M4: optimizacija i paralelizacija
Prema [OPTIMIZATION.md](OPTIMIZATION.md): prvo nivo 1 (tehnike iz laba), tek onda nivo 2. Svaka izmjena
se poredi s osnovom i upisuje u [PERF_LOG.md](PERF_LOG.md). Milestone-ovi za ovu fazu se
razrade posebno kad serijska faza bude gotova.

### M-VIZ: vizualizacija u rendereru (ZADNJI milestone cijelog projekta)
Radi se tek kad je solver gotov, optimiziran i izmjeren. Do tada se dim provjerava konzolnim programom
i slikama presjeka (`.pgm`, vidi [CLI_OUTPUT.md](CLI_OUTPUT.md)), bez renderera.
Razlog što može čekati: Solver je odvojen i renderer čita samo `density()` (ARCHITECTURE.md).
- [ ] `Integration/` s `SmokeScene` registrovanom u `test-app`.
- [ ] `Texture3D` i prenos gustoće svaki frejm (`glTexSubImage3D`).
- [ ] Volumni prolaz: ray marching kroz kocku, prozirnost (Beer-Lambert), crtanje u HDR FBO u
      `OnPostRender`. Reference: [Crane07].
- [ ] ImGui panel: izbor veličine mreže (32/64/96/128 + Apply), parametri, pauza, jedan korak, reset.

**Kriterij:** dim se vidi u realnom vremenu u rendereru, a veličina mreže se mijenja iz menija.

---

## 3. Otvorene odluke (donijeti u M0)

| ID | Pitanje | Preporuka |
| :--- | :--- | :--- |
| D1 | Uvodimo li polje temperature (Fedkiw) ili samo gustoću? | Uvesti, uz mogućnost isključivanja (`β = 0`) |
| D2 | Ghost sloj ili provjere rubova u petljama? | Ghost sloj (kao [Stam03]) |
| D3 | Vrh kutije zatvoren ili otvoren? | Zatvoren u serijskoj fazi, otvoren kasnije kao opcija |
| D4 | Jedinice: ćelije (`h = 1`) ili fizička veličina? | Jedinice ćelije (jednostavnije), znajući da se parametri ne prenose automatski između veličina mreže |
| D5 | Alat za testove | doctest preko `FetchContent` |
| D6 | Jezik komentara u kodu | Engleski (kao Renderer); objašnjenja u razgovoru i dokumentima na bosanskom |
| D7 | Jezik i kodiranje ispisa konzolnog programa ([CLI_OUTPUT.md](CLI_OUTPUT.md)) | Bosanski bez dijakritika (ASCII) |

---

## 4. Šta se NE radi u ovoj fazi

- OpenMP, SIMD, GPU, multigrid, red-black Gauss-Seidel (faza 5 našeg planskog dokumenta).
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
