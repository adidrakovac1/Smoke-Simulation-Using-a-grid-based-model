# CLAUDE.md: pravila za rad na ovom repozitoriju

Ovaj fajl Claude CLI čita automatski pri pokretanju u ovom folderu.

## Šta je ovo

Studentski projekat za predmet **Paralelni računarski sistemi** (ETF Sarajevo): 3D simulacija
dima na mreži ćelija (Eulerov pristup) u C++-u, koju kasnije paraleliziramo (OpenMP, SIMD,
eventualno GPU) i mjerimo ubrzanje. Dim prikazujemo u postojećem OpenGL rendereru.

**Trenutna faza: SAMO serijska verzija.** Paralelizaciju ne pišemo dok tim ne kaže, ali kod
pišemo tako da je ne otežava.

## Gdje je šta

- `Renderer/`: **tuđi kod** (OpenGL forward renderer prethodnog tima). **Ne mijenjati.**
  Jedine dozvoljene izmjene su navedene u `docs/plan/ARCHITECTURE.md`, odjeljak 6.
- `Solver/`: **naš kod**, simulacija. Čisti C++20, **bez OpenGL-a, GLFW-a, GLM-a i bez ikakvog
  headera iz Renderera**.
  **Skelet već postoji** (gradi se, 11 testova prolazi): javni interfejs u `Solver/include/smoke/`, faze su
  prazne funkcije s `TODO(M1)`/`TODO(M2)`/`TODO(M3)` u `Solver/src/stages/`. Gradi se i bez renderera:
  `cmake -S Solver -B Solver/build -G "MinGW Makefiles"`. Nadograđuj ga, ne prepravljaj strukturu bez razloga.
- `Integration/`: tek u zadnjem milestone-u (M-VIZ, vizualizacija); jedino mjesto koje uključuje i Solver i Renderer.
  Dim je u "nevidljivoj kutiji" koja se kasnije postavlja u scenu (`docs/plan/ARCHITECTURE.md`, odjeljak 8).
- `docs/`: literatura (PDF) i naši dokumenti u `docs/plan/`.

**Prije bilo kakvog rada pročitaj:** `docs/plan/ARCHITECTURE.md`, `PLAN.md`, `MATH.md`,
`VERIFICATION.md`. Ako se nešto u kodu kosi s njima, zaustavi se i pitaj, nemoj tiho odstupati.

## Okruženje (Windows)

- Kompajler: MSYS2 MinGW-w64 (UCRT64), `g++`; `mingw32-make`; CMake 3.26+.
- Gradnja Solvera (primjer): `cmake -S Solver -B Solver/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`
  pa `cmake --build Solver/build --parallel`.
- Putanja projekta **ne smije imati razmake**.
- Referentna gradnja: bez `-ffast-math`, bez `-march=native`.

## Pravila koda

- C++20. Imena i komentari u kodu na **engleskom** (kao u Rendereru).
- Namespace `smoke`. Serijske faze u `smoke::serial`.
- Polja su `float`, poseban niz po veličini, `i` je najbrži indeks, ghost sloj od jedne ćelije.
- Nema globalnog stanja ni `static` pomoćnih nizova u funkcijama; nema slučajnih brojeva u koraku.
- Svaka faza koraka mjeri svoje vrijeme.
- Novu zavisnost (biblioteku) ne dodavati bez pitanja.
- Ne izmišljati brojeve (pragove, konstante) koje moramo izmjeriti ili pročitati u radu: ostaviti
  `TODO(measure)` ili `TODO(paper: <rad, jednadžba>)` i reći korisniku.
- Git: grane `feature/<naziv>`, `bugfix/<naziv>`, `docs/<naziv>`; poruke `tip(opseg): opis`.
  Ne commitati bez da korisnik to traži.

## Optimizacija i mjerenje (obavezna pravila)

Detalji su u `docs/plan/OPTIMIZATION.md` i `docs/plan/PERF_LOG.md`. Ukratko:

- Redoslijed: **prvo nivo 1** (tehnike iz laba u `docs/reference/PRS_Vektorizacija/`: redoslijed petlji,
  `restrict`, auto-vektorizacija, `omp simd`, poravnanje, maske bez grananja, AVX2 s rezervnom putanjom,
  tiling), **tek poslije nivo 2** (OpenMP niti, fuzija jezgara, red-black, multigrid, GPU...). Ne prelazi na
  nivo 2 bez odobrenja i bez dokaza da je nivo 1 iscrpljen.
- **Jedna izmjena po koraku.** Prvo tačnost (testovi i poređenje s referentnim rezultatom), pa mjerenje.
- Svaka izmjena radi brzine mora imati: hipotezu, **poređenje s osnovom u istoj sesiji na istom računaru**
  (Release, zagrijavanje, najmanje 10 ponavljanja, medijan/minimum/raspon), ubrzanje **S** i deltu **Δ**,
  i unos u `PERF_LOG.md` te `perf/results.csv` u istom commitu. Zapiši i izmjene koje **nisu** pomogle.
- Ne objavljuj apsolutna vremena kao zaključak. Zaključci su u S, Δ i E (efikasnost memorije), uz otisak računara.
- Ne izmišljaj brojeve. Ako nešto nije izmjereno, napiši "nije izmjereno".
- Gradnja je uvijek Release; AVX2 samo po fajlu uz provjeru procesora u toku rada; bez `-march=native` i bez
  `-ffast-math` u referentnoj verziji.

## Kako objašnjavati (važno)

Korisnik **uči**, nije mu dovoljan samo kod. Za svaki dio koji pišeš objasni:
1. **šta je to** (npr. šta je ghost sloj, šta radi `project`),
2. **odakle dolazi** (koji rad i koja jednadžba, ili koja odluka u dokumentima),
3. **kako se povezuje** s ostalim dijelovima (ko ga poziva, šta mu daje).

Objašnjenja u razgovoru piši na **bosanskom**, jasno i redom. Prije većih koraka kratko reci šta
ćeš uraditi i zašto. Poslije svakog koraka reci kako se provjerava da radi.

## Radni tok

1. Za svaki milestone iz `PLAN.md` prvo predloži plan (fajlovi, funkcije, testovi), sačekaj odobrenje.
2. Piši test zajedno s kodom. Milestone je gotov tek kad je njegov kriterij završetka ispunjen.
3. Poslije izmjene pokreni izgradnju i testove i prijavi tačan rezultat (i ako nešto nije prošlo).
4. Kad se donese odluka (D1 do D7 ili nova), upiši je u odgovarajući dokument.
