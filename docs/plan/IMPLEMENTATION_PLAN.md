# IMPLEMENTATION_PLAN: M0, M1 i M2 (M3 i M4 skicirano)

Ovaj dokument kaže **tačno šta pišemo** u milestone-ima M0 do M2: fajlove, funkcije, redoslijed i testove.
*Šta* i *zašto* je u [PLAN.md](PLAN.md), [ARCHITECTURE.md](ARCHITECTURE.md), [MATH.md](MATH.md) i
[VERIFICATION.md](VERIFICATION.md). Ovdje je *kako*.

Oznake uloga: **[A]** jezgro solvera, **[B]** provjera i mjerenje, **[C]** pritisak (PLAN.md, odjeljak 1).
Uloge još nisu raspodijeljene, pa sve radi jedna osoba, a oznake služe da se posao kasnije lako podijeli.

Status: **odobreno 2026-11-09.** Prihvaćene su sve preporuke P1 do P5 (odjeljak 8).

---

## 0. Polazno stanje (provjereno 2026-11-09)

| Stavka iz M0 (PLAN.md) | Stanje | Dokaz |
| :--- | :--- | :--- |
| Svi pokreću Renderer | ne mogu provjeriti | provjerava svaki član na svom računaru |
| Fork, grane, pravila | upisano u PLAN.md i CLAUDE.md, a dogovor tima ne mogu provjeriti | |
| Odluke D1 do D8 | **gotovo** | PLAN.md, odjeljak 3 |
| Skelet `Solver/` | **gotovo** (commit 16b5f8f) | `cmake` konfiguriše i gradi u Releaseu, 11/11 testova prolazi; `smoke_cli` i `smoke_cli --selftest` rade (izlazni kod 0) |
| Kriterij: gradi se **na računaru svakog člana** | provjereno samo na ovom računaru (g++ 16.2.0, CMake 4.5.0-rc3) | |

Ono što iz M0 još fali je **prelazak na doctest** (odluka D5, odjeljak 2). Usput je uočeno: prvo pokretanje
tek izgrađenog `smoke_cli.exe` jednom je javilo `Permission denied`, a drugo je prošlo. Najvjerovatnije ga je
antivirus skenirao odmah poslije linkanja. Ovo nije greška u kodu; zapisujemo ga za slučaj da se ponovi.

---

## 1. Nalazi i nesaglasnosti (traže potvrdu prije implementacije)

Ništa od ovoga nije tiho promijenjeno. Uz svaki nalaz je prijedlog.

| # | Nalaz | Prijedlog |
| :-- | :--- | :--- |
| N1 | [Fedkiw01] jed. (4) i (5) imaju `Δt`: `∇²p = ∇·u*/Δt`, `u = u* − Δt∇p`. MATH.md koristi oblik iz [Stam03] bez `Δt`. Oba oblika su ekvivalentna: naš `p` je zapravo `Δt·p` iz rada. | Zadržati oblik iz [Stam03] i dodati napomenu u MATH.md, odjeljak 5, da naš `p` nije fizički pritisak, nego `Δt·p`. |
| N2 | [Fedkiw01] jed. (8) koristi `z` kao smjer "gore", a mi `+y` (ARCHITECTURE.md, odjeljak 8). | Napomena u MATH.md, odjeljak 3: uzgon djeluje na `v`. |
| N3 | MATH.md kaže da se tačka unazad "ograničava na unutrašnjost". [Stam03] `advect` ograničava na `[0.5, N+0.5]`, dakle do **zida** (pola ćelije u ghost sloj), i interpolira i iz ghost ćelija. [Fedkiw01] (sl. 2) isto siječe putanju na zidu. | Usvojiti `[0.5, n+0.5]` po svakoj osi i to precizno upisati u MATH.md, odjeljak 4. |
| N4 | [Stam03] `set_bnd` je 2D: popunjava stranice i 4 ugla (prosjekom). U 3D trilinearna interpolacija blizu ivice čita i **ghost ivice i uglove**, koje rad ne definiše. | `setBoundary` ide po osama redom (x, pa y, pa z), a svaki sljedeći prolaz pokriva i ghost redove prethodnih osa. Tako se ivice i uglovi popune istim pravilom (kopija ili promjena znaka), bez posebnih slučajeva. Odstupanje od Stamovog prosjeka u uglovima upisati u MATH.md, odjeljak 6. |
| N5 | T6 (simetrija) ne može biti **bit po bit**: zrcalna koordinata `(n+1−i) + dt·u` se drugačije zaokružuje nego `i − dt·u`. | T6 s tolerancijom; tolerancija je `TODO(measure)`. Upisati u VERIFICATION.md. |
| N6 | T1: kod linearne interpolacije s konstantnom brzinom težište se pomjera **tačno** za `c·dt` (dokaz u odjeljku 4.4), dok god je kugla daleko od zidova. Zato T1 može imati usku toleranciju (greška zaokruživanja), a ne "grešku interpolacije". | Tolerancija za T1 je `TODO(measure)`, a očekivani red veličine je greška zaokruživanja `float`. Upisati u VERIFICATION.md. |
| N7 | `SmokeSource` ima relativnu poziciju ("da se ne mijenja s veličinom mreže"), ali `radiusCells` je u ćelijama, što se kosi s tim. | Preimenovati u `radius`, kao udio od `nx` (relativno, kao pozicija). **Pitanje P2.** |
| N8 | `SmokeSource::temperature`: nije rečeno je li to brzina dodavanja ili ciljna vrijednost. [Stam03] `add_source` i [Stam99] `addForce` **dodaju** `dt·s`. | Isti oblik za oba polja: `ρ += dt·densityRate`, `T += dt·temperatureRate`. Polje se preimenuje u `temperatureRate`. **Pitanje P2.** |
| N9 | Javni interfejs nema načina da se postavi početno stanje (kugla gustoće, konstantna brzina). Kriterij M1 traži da se pomjeranje kugle **vidi kao slika**. | Demo ide u test biblioteku (`smoke_selftest`, koja smije koristiti unutrašnjost), a `smoke_cli --demo advect` ga samo pokreće, isto kao `--selftest`. Javni interfejs ostaje čist. **Pitanje P1.** |
| N10 | Dijagnostika (max\|div\| poslije, masa, NaN) traži dodatne prolaze kroz mrežu. Ako se računa unutar faza, to ulazi u mjereno vrijeme faza. | `SmokeSolver::diagnostics()` računa **na zahtjev**, izvan tajmera faza. `max|div|` prije uzima iz polja `divergence`, koje je projekcija već popunila. Potpis se mijenja u `Diagnostics diagnostics() const` (vraća vrijednost). **Pitanje P3.** |
| N11 | MATH.md, odjeljak 2: "Isti redoslijed imaju [Stam99] i [Fedkiw01]". To vrijedi za [Stam99] i [Fedkiw01], ali [Stam03] `vel_step` projicira **dva puta** (prije i poslije advekcije). | Mi projiciramo jednom (kao [Fedkiw01]); dodati rečenicu u MATH.md. Druga projekcija je kasnija opcija za kvalitet, ne za M2. |
| N12 | Putanja unazad: [Stam03] koristi jedan Eulerov korak `x − dt·u(x)`, a [Stam99] RK2. MATH.md koristi Eulerov. | Zadržati Eulerov korak (MATH.md); RK2 je kasnija opcija za kvalitet. |
| N13 | Početna vrijednost pritiska: [Stam03] svaki korak kreće od `p = 0`. Kretanje od prošlog `p` ("topli start") brže konvergira, ali mijenja rezultate. | `p = 0` kao u radu (jednostavno, determinističko, čist T3). Topli start je kandidat za M4b. |
| N14 | PLAN.md, M0: stavka "Skelet `Solver/`" je još neoznačena, iako je gotova. | Označiti `[x]` poslije odobrenja. |

---

## 2. M0: zatvaranje (prelazak na doctest) **[B]**

**Šta je doctest:** biblioteka za testove u jednom headeru. Test se piše kao `TEST_CASE("ime") { CHECK(a == b); }`,
a doctest sam broji, pokreće i ispisuje rezultate. Odluka D5: header ide u repozitorij, bez preuzimanja pri gradnji.

### Fajlovi
| Fajl | Izmjena |
| :--- | :--- |
| `Solver/third_party/doctest/doctest.h` | **novo**: zvanični header, izdanje 2.4.x (tačna verzija se upisuje u `VERSION`) |
| `Solver/third_party/doctest/LICENSE.txt`, `VERSION` | **novo**: MIT licenca i broj verzije |
| `Solver/CMakeLists.txt` | `smoke_selftest` dobija `third_party/` (kao `SYSTEM`, da tuđa upozorenja ne smetaju) i `src/` (testovi testiraju unutrašnje faze) |
| `Solver/tests/selftest.cpp` | `DOCTEST_CONFIG_IMPLEMENT` + `int smoke_test::runAll(std::ostream&)` koji pravi `doctest::Context` i pokreće testove |
| `Solver/tests/test_*.cpp` | `SMOKE_TEST(x)` postaje `TEST_CASE("x")`; `CHECK` ostaje isti |
| `Solver/tests/check.h` | **briše se** |

- `smoke_tests` i `smoke_cli --selftest` i dalje pokreću iste testove (razlog: Windows na nekim računarima
  blokira `smoke_tests.exe`). OBJECT biblioteka ostaje iz istog razloga kao do sada: samoregistracija testova.
- Doctest piše svoj izvještaj na engleskom (ASCII). Ispod njega dodajemo naš sažetak na bosanskom
  (`Ukupno: ... Status: OK`), kao u CLI_OUTPUT.md.
- Header se preuzima jednom, s `github.com/doctest/doctest` (ovo traži internet samo tada).

**Provjera:** `cmake --build Solver/build` bez novih upozorenja; `smoke_tests` i `smoke_cli --selftest` prijave
11/11 testova i izlazni kod 0.

---

## 3. Zajednička pravila za M1 i M2

Ova pravila slijede iz ARCHITECTURE.md, odjeljak 5, i važe za svaku fazu.

1. **Ghost sloj je uvijek ažuran na kraju faze.** Svaka faza koja upiše polje na kraju zove `setBoundary` za to
   polje. Tako sljedeća faza smije čitati susjede (i ghost ćelije) bez razmišljanja. T5 to provjerava.
2. **Dvostruko baferovanje.** Prije advekcije ili Jacobija polje se zamijeni sa svojim parom `*0`
   (`swap`, samo zamjena pokazivača), pa se čita iz `*0`, a piše u glavno polje.
3. **Petlje:** `for k in [range.k0, range.k1)`, pa `for j in [1, ny]`, pa `for i in [1, nx]` (unutrašnja,
   najbrži indeks). Bez `if`-ova za rubove, bez virtualnih poziva. Pomoćne funkcije koje se zovu po ćeliji su
   `inline` u headeru.
4. **`setBoundary` radi na cijelom polju**, a ne po `SlabRange`. U serijskoj verziji to ništa ne mijenja; u
   kasnijoj paralelnoj verziji se zove poslije sinhronizacije niti.
5. **Bez novih zavisnosti.** Za slučajne brojeve u testovima (T3) pišemo mali vlastiti generator (splitmix64),
   jer `std::uniform_real_distribution` ne daje iste brojeve na različitim kompajlerima.

---

## 4. M1: osnova

**Cilj (kriterij iz PLAN.md):** kugla gustoće u polju konstantne brzine pomjera se ispravno (T1) i simetrično
(T6), bez NaN, i to se vidi na slici `.pgm`.

### 4.1 Fajlovi i potpisi

| Fajl | Šta | Uloga |
| :--- | :--- | :--- |
| `include/smoke/Field3D.h`, `src/Field3D.cpp` | `void swap(Field3D& other) noexcept;` i slobodna `swap(Field3D&, Field3D&)` | [A] |
| `include/smoke/SmokeParams.h` | `SmokeSource`: `radius` (udio od `nx`), `densityRate`, `temperatureRate` (po P2); vrijednosti `TODO(measure)` | [A] |
| `src/stages/Boundary.cpp` | `void setBoundary(Field3D& f, BoundaryKind kind);` (prolazi po osama, N4) | [A] |
| `src/stages/Stages.h` | dodati `void setVelocityBoundary(SmokeState& s);` (u→X, v→Y, w→Z) | [A] |
| `src/stages/Interpolation.h` | **novo**: `inline float sampleTrilinear(const Field3D& f, float x, float y, float z);` (koordinate u indeksima ćelija, poslije ograničenja na `[0.5, n+0.5]`) | [A] |
| `src/stages/Advection.cpp` | `void advectScalar(Field3D& dst, const Field3D& src, const Field3D& u, const Field3D& v, const Field3D& w, float dt, SlabRange r);` i `advectScalars(...)` (swap, advectScalar za ρ i T, setBoundary) | [A] |
| `src/stages/Sources.cpp` | `addSources(...)`: ćelije unutar kugle izvora dobiju `+= dt·rate` | [A] |
| `apps/common/PgmWriter.h` | **novo**, samo header: `bool writePgmSliceXY(const std::string& path, const DensityView& v, int k, float maxValue);` (slika presjeka x–y na sloju `k`, gore je veće `y`) | [B] |
| `tests/demo_advect.cpp`, `tests/selftest.h` | **novo**: `int smoke_test::runAdvectionDemo(const std::string& folder, std::ostream& out);` (P1) | [B] |
| `apps/smoke_cli/main.cpp` | `--dump-slices FOLDER`, `--every M`, `--demo advect` | [B] |
| `tests/test_helpers.h` | **novo**: punjenje kugle, težište, `maxAbsDiff`, mjera asimetrije, splitmix64 | [B] |

### 4.2 Pojmovi koji se uvode u M1 (objašnjavam ih detaljno pri pisanju)

- **Ghost sloj i `setBoundary`** ([Stam03], `set_bnd`; MATH.md, odjeljak 6). Za skalare (gustoća, temperatura,
  pritisak) ghost ćelija dobije vrijednost susjedne unutrašnje ćelije (nulti gradijent). Za brzinu komponenta
  okomita na zid dobije **suprotan znak**, pa je prosjek na zidu 0 (nema protoka kroz zid); paralelne komponente
  se kopiraju.
- **Semi-Lagrangian advekcija** ([Stam99], odjeljak 2.2 i dodatak A; [Stam03], `advect`; MATH.md, odjeljak 4):
  za centar ćelije `(i,j,k)` idemo unazad `x = i − dt·u`, `y = j − dt·v`, `z = k − dt·w` (uz `h = 1`, odluka D4),
  ograničimo na `[0.5, n+0.5]` i trilinearno interpoliramo iz starog polja.
- **Trilinearna interpolacija:** linearna interpolacija po x, pa po y, pa po z, iz 8 susjednih ćelija. Težine
  su nenegativne i daju zbir 1, pa je rezultat između najmanje i najveće od 8 vrijednosti (T4).
- **Mapiranje relativne pozicije u indeks:** unutrašnja ćelija `i` pokriva `[i−0.5, i+0.5]`, pa kutija ide od
  `0.5` do `n+0.5`. Relativni `x ∈ [0,1]` je indeks `0.5 + x·nx`. Centar `x = 0.5` daje `(nx+1)/2`, što je tačno
  ogledalo za T6.

### 4.3 Redoslijed pisanja (svaki korak ima test u istom koraku)

1. `Field3D::swap` i test (pokazivači zamijenjeni, veličine zamijenjene). [A]
2. `setBoundary` i **T5** (dio za skalar i brzinu): polje popunjeno poznatim vrijednostima, pa provjera svih
   6 stranica, ivica i uglova. [A]
3. `sampleTrilinear` i testovi: tačno u centru ćelije daje vrijednost ćelije; za linearno polje `f = a·x+b·y+c·z`
   daje tačnu vrijednost; ograničenje na granici. [A]
4. `advectScalar` i:
   - **T2**: `u = 0`, više koraka, gustoća **bit po bit** ista (težine su tačno 1 i 0);
   - **T4**: proizvoljna gustoća i brzina, svaka nova vrijednost unutar `[min, max]` 8 izvornih ćelija;
   - **T1**: kugla u `u = (c,0,0)`, jedan korak i N koraka, pomak težišta = `c·dt` po koraku. [A/B]
5. `addSources` i test: masa poraste tačno za `dt·densityRate·(broj ćelija u kugli)`; van kugle nema promjene. [A]
6. Povezivanje u `step` (izvor, pa advekcija skalara; ostale faze su još prazne) i:
   - **T6**: simetričan scenarij (izvor ili kugla u sredini po x i z, brzina simetrična), poređenje `ρ(i,j,k)`
     i `ρ(nx+1−i, j, k)` s tolerancijom (N5);
   - **T7**: dva solvera, isti ulaz, `memcmp` gustoće poslije N koraka. [B]
7. `PgmWriter.h`, `--dump-slices`, `--every` i `--demo advect`. [B]

### 4.4 Zašto T1 može biti strog (N6)

Neka je pomak po koraku `c·dt = n + s` (`n` cijeli dio, `s ∈ [0,1)`). Daleko od zidova linearna interpolacija daje
`ρ'(i) = (1−s)·ρ(i−n) + s·ρ(i−n−1)`. Težište:
`Σ i·ρ'(i) = (1−s)·Σ (j+n)·ρ(j) + s·Σ (j+n+1)·ρ(j) = M·(C + n + s)`, gdje je `M` masa i `C` staro težište.
Masa se ne mijenja, a težište se pomjeri **tačno** za `c·dt`. Odstupanje dolazi samo od zaokruživanja i od
zidova, pa kuglu u testu držimo daleko od zidova.

### 4.5 Scenarij za demo i T1 (izbor scenarija, ne fizička konstanta)

Mreža 64³, kugla poluprečnika 8 ćelija s centrom u `(20, 32.5, 32.5)`, `u = (c,0,0)`, `dt = 1/60` (zadano u
`SmokeParams`), `c = 30` ćelija/s, pa je pomak 0.5 ćelije po koraku; 40 koraka daje 20 ćelija. Kugla je cijelo
vrijeme najmanje 11 ćelija od svakog zida. Ovi brojevi su izbor testa (da se vidi pomak i da je daleko od zidova), ne
vrijednosti iz rada.

### 4.6 Kako se provjerava da je M1 gotov

| Tačka kriterija | Komanda | Šta treba vidjeti |
| :--- | :--- | :--- |
| Testovi T1, T2, T4, T5, T6, T7 | `Solver/build/smoke_tests.exe` (ili `smoke_cli --selftest`) | svi `PROSAO` / doctest `SUCCESS`, izlazni kod 0 |
| Pomak se vidi kao slika | `smoke_cli --demo advect --dump-slices out/m1 --every 10` | `out/m1/*.pgm`: kugla ide udesno, oblik ostaje okrugao (blago razmazan) |
| Bez NaN | isti demo | ispis `NaN/Inf celija: 0`, `Status: OK` |

`.pgm` se otvara u GIMP-u ili IrfanView-u, ili u VS Code-u s dodatkom za slike.

---

## 5. M2: brzina i pritisak

**Cilj (kriterij iz PLAN.md):** dim se diže kao stub i uvija se; divergencija poslije projekcije je izmjerljivo
manja nego prije i opada s brojem iteracija; 500 koraka bez NaN; prag za T3 izmjeren i upisan.

### 5.1 Fajlovi i potpisi

| Fajl | Šta | Uloga |
| :--- | :--- | :--- |
| `src/stages/Forces.cpp` | `void applyBuoyancy(SmokeState& s, const SmokeParams& p, float dt, SlabRange r);` (poziva je `applyForces`; `v += dt·(−α·ρ + β·(T − T_amb))`), pa `setBoundary(v, VelocityY)` | [A] |
| `src/stages/Advection.cpp` | `advectVelocity(...)`: swap `u↔u0`, `v↔v0`, `w↔w0`; svaka komponenta se advektira **starim** poljem `(u0,v0,w0)` pomoću `advectScalar`; `setVelocityBoundary` | [A] |
| `src/stages/Projection.cpp` | `void computeDivergence(const Field3D& u, const Field3D& v, const Field3D& w, Field3D& div, SlabRange r);` | [C] |
| | `void jacobiIteration(Field3D& pNew, const Field3D& pOld, const Field3D& div, SlabRange r);` | [C] |
| | `void subtractPressureGradient(Field3D& u, Field3D& v, Field3D& w, const Field3D& p, SlabRange r);` | [C] |
| | `project(...)`: divergencija → `p = 0` → K × (swap, Jacobi, `setBoundary(p, Scalar)`) → oduzimanje gradijenta → `setVelocityBoundary` | [C] |
| `src/DiagnosticsCompute.h/.cpp` | **novo**: `Diagnostics computeDiagnostics(const SmokeState& s, float dt);` (max\|div\| prije iz `s.divergence`, poslije računato iznova, masa, max\|u\|, CFL, broj NaN/Inf) | [B] |
| `include/smoke/SmokeSolver.h` | `Diagnostics diagnostics() const;` računa na zahtjev (P3) | [B] |
| `apps/smoke_cli/main.cpp` | `--dt X`, `--jacobi K`; u liniji napretka `max|div| prije -> poslije` i masa; izlazni kod 1 ako ima NaN/Inf | [B] |

### 5.2 Pojmovi koji se uvode u M2

- **Uzgon** ([Fedkiw01], jed. 8; MATH.md, odjeljak 3): sila na gore je `−α·ρ + β·(T − T_amb)`, a mi je
  primjenjujemo na `v`, jer je `+y` "gore" (N2). Dodaje se kao `v += dt·f` ([Fedkiw01], odjeljak 4;
  [Stam99], `addForce`).
- **Advekcija brzine** ([Stam99], odjeljak 2.2, "self-advection"): ista funkcija kao za gustoću, samo je polje
  koje se nosi jedna komponenta brzine. Čita se iz kopije `*0` jer bi se inače polje mijenjalo dok ga čitamo.
- **Projekcija** ([Stam99], dodatak B; [Stam03], `project`; MATH.md, odjeljak 5):
  - divergencija `div = (u(i+1) − u(i−1) + v(j+1) − v(j−1) + w(k+1) − w(k−1)) / 2`;
  - Jacobi `p_novo = (p(i±1) + p(j±1) + p(k±1) − div) / 6`, uz `setBoundary(p, Scalar)` poslije svake
    iteracije (Neumannov uslov `∂p/∂n = 0`, [Fedkiw01], jed. 4);
  - korekcija `u −= (p(i+1) − p(i−1))/2` i isto za `v`, `w`.
  Ovaj predznak je provjeren prema [Stam03] (`div = −0.5·h·(...)`, `p = (div + Σ)/4`): naš `div` ima suprotan znak,
  pa je `p = (Σ − div)/6` ista jednačina. T3 to potvrđuje i brojevima.
- **Zašto divergencija ne pada na nulu** (MATH.md, odjeljak 5, napomena): "gradijent pa divergencija" sa
  centralnim razlikama daje širi stencil (razmak 2), a Jacobi rješava uži (7 tačaka). Zato T3 traži omjer i
  opadanje, a ne nulu.

### 5.3 Redoslijed pisanja

1. `applyBuoyancy` i test: s `α = 0`, `β = 1`, `T − T_amb = 1` u jednoj ćeliji, `v` poraste tačno za `dt`;
   `u` i `w` se ne mijenjaju. [A]
2. `advectVelocity` i test: konstantno polje `(c,0,0)` daleko od zidova ostaje konstantno. [A]
3. `computeDivergence` i test na analitičkom polju (`u = x` daje `div = 1` u unutrašnjosti). [C]
4. `jacobiIteration`, `subtractPressureGradient`, `project` i **T3**: slučajno polje brzine (splitmix64,
   fiksno sjeme), projekcija s K = 10, 20, 40, 80. Provjera: omjer `max|div|` poslije / prije ispod praga i
   strogo opada s K. **Prag je `TODO(measure)`:** prvo izmjerimo omjere na 32³ i 64³, pa prag upišemo s datumom
   i veličinom mreže (VERIFICATION.md, "O pragovima"). [C]
5. `computeDiagnostics` i `diagnostics()`; test: masa i broj NaN tačni na ručno postavljenom polju. [B]
6. Cijeli `step` i:
   - **T8**: 500 koraka bez NaN/Inf. U `smoke_tests` na 32³ (da testovi ostanu brzi), a na 64³ kroz
     `smoke_cli` kao dio kriterija;
   - **T9**: proširiti postojeći test: poslije `resize` su **sva** polja nulirana, a ne samo gustoća. [B]
7. Izbor parametara za izgled: `α`, `β`, `T_amb`, izvor. **`TODO(measure)`:** biramo ih eksperimentalno
   gledajući slike i upisujemo u MATH.md s datumom (MATH.md, odjeljak 3: "biramo eksperimentalno"). [A/B]

### 5.4 Kako se provjerava da je M2 gotov

| Tačka kriterija | Komanda | Šta treba vidjeti |
| :--- | :--- | :--- |
| T3, T8 (32³), T9 i svi raniji | `smoke_tests.exe` | sve prolazi, izlazni kod 0 |
| Divergencija manja i opada s K | `smoke_cli --steps 50 --jacobi 20`, pa `--jacobi 40`, pa `--jacobi 80` | `max|div| prije -> poslije`: poslije je znatno manje, i to manje što je K veće |
| 500 koraka bez NaN (64³) | `smoke_cli --steps 500 --warmup 0` | `NaN/Inf celija: 0`, `Status: OK`, izlazni kod 0 |
| Stub koji se diže i uvija | `smoke_cli --steps 300 --dump-slices out/m2 --every 20` | dim se diže od izvora, širi se pri vrhu, rubovi se kovitlaju |

**Rizik za "uvija se":** bez vorticity confinementa (M3) i bez ikakvog šuma, simetričan izvor na 64³ može dati
prilično gladak stub (gljivast vrh, slabo kovitlanje). To je poznata posljedica numeričke difuzije ([Stam03],
"numerical dissipation"; [Fedkiw01], odjeljak 3). Ako se to desi, prijavljujem kao nalaz, a rješenje je u M3.

---

## 6. M3 i M4 (skica)

- **M3 [A/B]:** vorticity confinement ([Fedkiw01], jed. 9 do 11; `ε` je `TODO(measure)`), podešavanje
  parametara, `smoke_bench` (CSV po fazi za 32³, 64³, 96³, 128³; mjerila kao `bench.hpp` iz laba),
  `--calibrate`, `--csv`, `--verify`, referentni rezultat `Solver/reference/serial_64_100steps.bin`, osnove
  B0/B1, T10 (prag `TODO(measure)`), vrijeme po Jacobi iteraciji.
- **M4 (sekvencijalno, D8):** M4a tehnike iz laba po jezgrima, redom po profilu iz M3; M4b dodatne sekvencijalne
  tehnike (uključujući topli start pritiska, N13); M4c zatvaranje. Sve po OPTIMIZATION.md i PERF_LOG.md.

---

## 7. Spisak vrijednosti koje ne izmišljamo

| Vrijednost | Oznaka u kodu | Kada se određuje |
| :--- | :--- | :--- |
| Tolerancija T1 | **izmjereno 2026-11-09:** greška 0 poslije 1 koraka i 4.9e-10 ćelije poslije 40; masa 2.4e-8 relativno. Granica 1e-6 | M1, gotovo |
| Tolerancija T6 | **izmjereno 2026-11-09:** relativna asimetrija 1.8e-6 (oko 15 ulp). Granica 1e-5; namjerna greška od pola ćelije daje 0.34 | M1, gotovo |
| Prag T3 (omjer max\|div\|) | `TODO(measure)` | M2, na 32³ i 64³ |
| `α`, `β`, `T_amb` | `TODO(paper: Fedkiw01, eq. 8)` + `TODO(measure)` | M2, eksperimentalno (rad ne daje brojeve) |
| Izvor: pozicija, radius, `densityRate`, `temperatureRate` | `TODO(measure)` | M2, eksperimentalno |
| `ε` (vorticity) | `TODO(measure)` | M3 |
| Prag T10 | `TODO(measure)` | M3 |

`dt = 1/60` i `jacobiIterations` u rasponu 20 do 80 dolaze iz našeg planskog dokumenta, pa nisu izmišljeni.

---

## 8. Pitanja za odobrenje

- **P1 (N9):** demo advekcije ide u `smoke_selftest`, a pokreće se sa `smoke_cli --demo advect`. Alternativa je
  javni API za postavljanje početnog stanja (`SmokeSolver::setScenario(...)`), koji bi trajno proširio interfejs.
  *Preporuka: demo u test biblioteci.*
- **P2 (N7, N8):** `SmokeSource` postaje `radius` (udio od `nx`) i `temperatureRate` (dodaje se `dt·rate`,
  kao [Stam03] `add_source`). *Preporuka: da.*
- **P3 (N10):** `diagnostics()` računa na zahtjev, izvan tajmera faza, i vraća vrijednost. *Preporuka: da.*
- **P4:** doctest preuzimam s GitHuba (jednom) i stavljam u `Solver/third_party/doctest/`. *Treba tvoje "da"
  za preuzimanje.*
- **P5:** napomene N1 do N6 i N11 do N13 upisujem u MATH.md i VERIFICATION.md, a N14 u PLAN.md. *Preporuka: da.*
