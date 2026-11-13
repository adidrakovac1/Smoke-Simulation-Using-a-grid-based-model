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

**Stanje (2026-11-09, odluka D11):** M4a urađen za Jacobi, projekciju, advekciju i vorticity confinement
(M4-001 do M4-014, M4-018 do M4-020); od M4b urađena fuzija jezgara u advekciji (M4-015 do M4-017); zbir prema B0/B1 je u
PERF_LOG.md, odjeljak 7. Ostale stavke ispod su namjerno ostavljene (D11).

**M4a: tehnike iz laboratorijske vježbe 1** (OPTIMIZATION.md, odjeljak 2, nivo 1)
- [ ] Za svako bitno jezgro (Jacobi, divergencija/korekcija, advekcija, uzgon, redukcije) prođeni
      koraci 2 do 10 iz OPTIMIZATION.md, odjeljak 2.3: redoslijed petlji, `restrict`, izvještaj
      auto-vektorizacije, `omp simd` (`-fopenmp-simd`, bez niti), poravnanje, maske bez grananja,
      blokiranje, AVX2 s rezervnom putanjom, više akumulatora.
      **Djelimično (stanje 2026-11-11):** urađeno za Jacobi, advekciju i vorticity confinement; divergencija i
      gradijent imaju `restrict` (M4-007), bez AVX2. Namjerno nije urađeno (D11): ručni gather, AVX2 za
      divergenciju i gradijent, poređenje poravnato/neporavnato (T11), više akumulatora za redukcije.
- [x] Svaki korak: testovi i poređenje s referentnim rezultatom, mjerenje prema osnovi (S, Δ),
      unos u evidenciju, i kad nije pomoglo. (M4-001 do M4-020 u PERF_LOG.md, uključujući odbačene.)

**M4b: dodatne sekvencijalne optimizacije** (izvan laba; OPTIMIZATION.md, odjeljak 3.1)
- [x] Počinje tek kad je M4a iscrpljen (kriterij iz OPTIMIZATION.md, odjeljak 2.3) i uz odobrenje tima.
      (Odobreno odlukom D11 uz skraćeni opseg M4a.)
- [ ] Kandidati: fuzija jezgara, vremensko blokiranje Jacobija, red-black Gauss-Seidel,
      multigrid / konjugirani gradijent, FMA, manji tip podataka, AVX-512 (ako ga hardver ima),
      prefetch. Bira se po profilu; nije obaveza uraditi sve.
      **Stanje 2026-11-11:** urađena fuzija jezgara u advekciji (M4-015 do M4-017). AVX-512 otpada (PC-2 ga nema).
      Red-black GS zavisi od P2 (D15), multigrid se ne radi (D15); ostalo čeka profil iz M4d (tačka G0).

**M4c: zatvaranje faze**
- [ ] Pregled koda među članovima, očišćeni TODO-ovi.
      (2026-11-12, automatski dio: u `Solver/`, `Integration/`, `tools/` i `perf/` nema oznaka TODO ni FIXME;
      u Solveru nema OpenGL-a, GLM-a ni headera Renderera, nema `static` ni globalnog stanja, nema slučajnih
      brojeva, nema niti ni `omp` pragmi. Pregled **među članovima tima** nije urađen; to je posao ljudi.)
- [x] Nacrt izvještaja: opis algoritma, tačnost, tabela serijskih vremena **i** tabela ubrzanja
      (S, Δ, E) po tehnici, prema osnovama B0/B1.
      (2026-11-12: [REPORT_DRAFT.md](REPORT_DRAFT.md). To je skelet s brojevima prepisanim iz ostalih dokumenata;
      mjesta za tim su označena s [DOPUNITI].)
- [ ] Sve odluke i izmjerene vrijednosti upisane u dokumente.
- [x] Granica `SlabRange` i dalje poštovana (da kasnija paralelizacija ne traži prepravljanje).
      (2026-11-12: do sada to nijedan test nije provjeravao. Dodan `test_slab_range.cpp`: jezgra pozvana u
      komadima daju bit po bit isto, u gradnji s AVX2 i bez njega; ukupno 62 testa. Nalaz: složene funkcije faza
      se ne smiju zvati po komadima, raspon se dijeli unutar njih; `setBoundary` i liste prepreka nemaju raspon.
      ARCHITECTURE.md, odjeljak 5, pravilo 1.)

**Kriterij:** za svako bitno jezgro nivo 1 je iscrpljen i upisan; dodatne tehnike koje su probane
su izmjerene i upisane (zadržane ili odbačene); optimizirana verzija prolazi sve testove i poređenje
s referentnim rezultatom; postoji tabela ubrzanja serijske verzije u odnosu na B0 i B1.

### M4d: profiliranje serijske verzije (AMD uProf; odluka D14)
Radni plan: [EXECUTION_PLAN.md](EXECUTION_PLAN.md), faze P0 do P2. Još uvijek sekvencijalno, bez niti (D8).
- [x] P0: popraviti kalibraciju memorije (triad), `smoke_bench --hwinfo`, ažurirati README i ovaj plan.
      (2026-11-11: triad je bio 6 puta prevelik; sada 17.9 GB/s uz teorijskih 38.4 GB/s; E za Jacobi na 128³ je
      oko 0.94. PROFILE.md, odjeljci 1 i 2; PERF_LOG.md, odjeljak 8.)
- [x] P1: AMD uProf instaliran, `Solver/build-prof` (`-g -fno-omit-frame-pointer`), `smoke_prof`, tbp i brojači na 64³ i 128³,
      `tools/uprof_report.py` pravi samostalan HTML, `docs/plan/PROFILE.md` s hardverom i odgovorima na hipoteze H1 do H5.
      (2026-11-11: vremensko uzorkovanje i hardverski brojači (`assess_ext`, `inst_access`); HTML je
      `perf/uprof/report.html`. IBS nije dostupan jer VBS drži hipervizor, PROFILE.md odjeljak 3.3. H2 je ostala otvorena.
      **Tačka G0 (Ermin): zasad se ništa ne optimizira**, ide se na M-VIZ; kandidati ostaju u PROFILE.md, odjeljak 5.)
- [x] P2: tabela ostatak/divergencija naspram K (32³, 64³, 128³) u MATH.md; odluka D15 o rješavaču.
      (2026-11-11: tabela je izmjerena, `smoke_bench --convergence`, MATH.md odjeljak 5. G1: Jacobi ostaje.)
- [x] **Zadnja stavka, tek kad je sve ostalo gotovo (zadnji korak prije paralelizacije; Ermin, 2026-11-11):**
      prepraviti HTML izvještaj profila (`tools/uprof_report.py`) da ima ljepši i jednostavniji grafički prikaz,
      po uzoru na Intel VTune. Do tada se izvještaj ne dotjeruje.
      (2026-11-13: urađeno iz postojećih 36 sesija, bez novog skupljanja. Kartice Sažetak, Hotspots,
      Mikroarhitektura, Keš i memorija, Instrukcije, Poređenje; opis i šta nedostaje jer podataka nema:
      PROFILE.md, odjeljak 3.4. Čeka Erminov pregled.)

**Kriterij:** postoji HTML izvještaj s hardverskim brojačima i PROFILE.md; Jacobi je potvrđen ili je red-black
eksperiment odobren.

### Paralelizacija: nije milestone (odluka D8)
OpenMP niti, GPU i ostale paralelne tehnike (OPTIMIZATION.md, odjeljak 3.2) se **ne planiraju** u ovoj
fazi. Kad tim odluči da počinje, za to se pišu posebni milestone-ovi. Do tada se paralelni kod ne piše.

### M-VIZ: vizualizacija u rendereru (poslije M4d; odluke D14 i D17)
Radi se tek kad je solver gotov, sekvencijalno optimiziran i izmjeren. Do tada se dim provjerava konzolnim programom
i slikama presjeka (`.pgm`, vidi [CLI_OUTPUT.md](CLI_OUTPUT.md)), bez renderera.
Razlog što može čekati: Solver je odvojen i renderer čita samo `density()` (ARCHITECTURE.md).
- [x] `Integration/` s `SmokeScene` registrovanom u `test-app`.
- [x] `Texture3D` i prenos gustoće svaki frejm (`glTexSubImage3D`, direktno iz solverovog niza, bez međukopije).
- [x] Volumni prolaz: ray marching kroz kocku, prozirnost (Beer-Lambert), crtanje u HDR FBO u
      `OnPostRender`. Reference: [Crane07]. Dodano: sjena u dimu (zraka prema svjetlu).
- [x] ImGui panel: izbor veličine mreže (32/64/96/128 + Apply), parametri, pauza, jedan korak, reset.

**Kriterij:** dim se vidi u realnom vremenu u rendereru, a veličina mreže se mijenja iz menija.

**Stanje 2026-11-11 (tačka G2 prošla: Ermin je pogledao demo, bez ispravki):** na PC-2 (RTX 4060 Ti) 64³ radi 60 FPS (ograničeno
vsync-om), 128³ oko 11 FPS. Slike: `docs/plan/img/mviz_64.png`, `mviz_128.png`. Provjereno automatskim
pokretanjem (260 koraka, slika prozora); promjena mreže provjerena kroz `resize()` na 128³, a dugmad panela
nisu kliknuta rukom. Objašnjenje i ograničenja: `Integration/README.md`.

Razrada: EXECUTION_PLAN.md, faza P3. Prva verzija scene je jednostavna: dim u zatvorenoj kutiji s vidljivim
okvirom, podom i izvorom pri dnu (D17).

### M-OBS: prepreke i mesh (poslije M-VIZ; odluke D13 i D16)
Razrada: EXECUTION_PLAN.md, faza P4; objašnjenja u `docs/plan/OBSTACLES.md`.
- [x] Parser `.obj`, voxelizacija (ljuska + poplava), `ObstacleGrid` (Fluid/Solid/Porous), `setObstacles()` u Solveru.
- [x] Fizika kroz rijetke liste graničnih ćelija (pritisak, brzina, gustina), porozni objekti (deka) s otporom.
      **Odstupanje od EXECUTION_PLAN P4.2, potvrditi na G3:** brzina u graničnim čvrstim ćelijama se odbija (kao na
      zidu kutije), ne postavlja na nulu; s nulom dim nije obilazio kuglu (OBSTACLES.md, odjeljak 4).
- [x] Testovi; mjerenje troška; bez prepreka `--verify` ostaje bit po bit isti kao PC-2 referenca.
      (2026-11-11: 10 novih testova, ukupno 58. Bez AVX2: 58/58 i `--verify` bit po bit. S AVX2: `--verify` bit po
      bit i isti hash gustine s preprekama. 2026-11-12: blokada Windows Smart App Control je prošla; AVX2 gradnja
      58/58 i `--verify` bit po bit. **Trošak izmjeren po protokolu D10** (PERF_LOG.md, odjeljak 9): bez prepreka
      nije mjerljiv; s kuglom ili dekom trošak cijelog koraka je ispod šuma te sesije, procjena za kuglu na 64³
      oko 3.6%.)
- [x] Integration: učitavanje `.obj` iz menija, crtanje mesha, test meshevi (kocka, kugla, deka).
      (Mesh se crta iz istih trouglova koje čita naš parser, ne kroz Assimp, da crtanje i voxelizacija dijele transformaciju.)
- [x] Renderer (D13): depth u teksturu, zaklanjanje dima; prozirni prolaz za deku.

**Kriterij:** dim se odbija od objekta, prolazi kroz poroznu deku usporen, objekt zaklanja dim iza sebe.

**Stanje 2026-11-11 (čeka tačku G3):** kriterij je ispunjen na 64³; slike su `docs/plan/img/mobs_*.png`
(kugla na 330 i 700 koraka, kocka i deka na 700). Dugmad panela nisu kliknuta rukom; scenariji su pokrenuti
automatski (`SMOKE_AUTOSHOT_OBSTACLE`).

**Tačka G3 (2026-11-12, Ermin): "možeš nastaviti".** Rad ide na P5. Ermin nije posebno komentarisao dva
odstupanja od plana (odbijanje brzine u čvrstim ćelijama; crtanje mesha bez Assimpa), pa ostaju kako su urađena;
ako ih želi drugačije, to je izmjena poslije G3. Demo rukom još nije pregledan.

### P5: zatvaranje (EXECUTION_PLAN.md)
- [x] Profil s preprekama (uProf, isti postupak kao P1), poređenje u HTML izvještaju.
      (2026-11-12: PROFILE.md, odjeljak 6. Prepreke su nova faza od 2.2 do 2.5% koraka za kuglu i 0.1% za deku;
      ostala jezgra se ne mijenjaju.)
- [x] M4c, dio koji može uraditi izvršitelj: automatski pregled koda, test za `SlabRange`, nacrt izvještaja
      (stavke su gore, pod M4c). Ostaje pregled koda među članovima tima.
- [ ] Označeni milestone-ovi; `CLAUDE.md` i `README.md` usklađeni sa stanjem.
      (2026-11-12: `README.md`, `docs/plan/README.md` i ovaj plan su usklađeni. `CLAUDE.md` još opisuje stanje od
      prije M-VIZ i 48 testova; mijenja se kad se grane spoje u `main`, uz Erminovu potvrdu.)

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
| D11 | Skraćeni opseg M4 (2026-11-09, Ermin) | Preostali mali koraci nivoa 1 (ručni gather za advekciju, AVX2 za divergenciju i gradijent, poravnanje T11) se **ne rade**: očekivani dobitak je mali i vezan za jedan procesor. Od nivoa 2 uzima se samo fuzija jezgara u advekciji (M4-015 do M4-017): ne zavisi od procesora i daje bit po bit isti rezultat. Tehnike koje mijenjaju rezultat (red-black GS, multigrid, FMA, 1/6 umjesto dijeljenja) ili su teške (vremensko blokiranje) ostaju za kasnije. Zbir M4 je u PERF_LOG.md, odjeljak 7 |
| D12 | `-fno-math-errno` po fajlu (2026-11-09) | Dozvoljen samo za `Forces.cpp` i `Forces_avx2.cpp` (M4-018), da se `std::sqrt` u petlji vektorizira. Ne mijenja nijedan rezultat (sqrt nenegativnog broja je tačan po IEEE 754; izostaje samo `errno` za negativan ulaz) i nije dio opcija `-ffast-math` koje mijenjaju vrijednosti. `-ffast-math`, `-fno-trapping-math` i FMA i dalje nisu dozvoljeni u referentnoj gradnji |
| D13 | `Renderer/` se smije mijenjati (2026-11-11, Ermin) | Prva potreba: depth u teksturu za zaklanjanje dima. Svaka izmjena se upisuje u ARCHITECTURE.md, odjeljak 6 |
| D14 | Redoslijed rada (2026-11-11) | M4d (profiling) pa M-VIZ pa M-OBS |
| D15 | Rješavač pritiska (2026-11-11) | Jacobi ostaje dok mjerenje (P2) ne pokaže da K = 40 na 128³ ne dostiže granicu tačnosti; tada red-black GS kao eksperiment, poređen po vremenu do iste divergencije. Multigrid se ne radi. **Zaključeno na G1 (2026-11-11, Ermin): Jacobi ostaje, red-black eksperiment se ne radi** (MATH.md, odjeljak 5) |
| D16 | Dizajn prepreka (2026-11-11) | Rijetke liste graničnih ćelija; vektorizirana jezgra netaknuta; vlastiti OBJ parser u Solveru; bez prepreka rezultat bit po bit isti kao referenca |
| D17 | Petlja simulacije i prva scena (2026-11-11) | Jedan korak po frejmu u glavnoj niti (D8); prva scena je jednostavna kutija s dimom |

---

## 4. Šta se NE radi u ovoj fazi

- OpenMP niti, GPU i bilo kakav višenitni kod (odluka D8). Nisu milestone.
- Optimizacija prije M4. U M1 do M3 pišemo jasan, ispravan kod; brzina dolazi tek u M4, poslije mjerenja.
- Izmjene u `Renderer/` mimo popisa u ARCHITECTURE.md, odjeljak 6 (odluka D13).
- Vizualizacija prije M4d (profiling dolazi prvi, D14).
- Zaklanjanje dima objektima scene prije M-OBS (tada se mijenja depth u Rendereru, D13).
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
