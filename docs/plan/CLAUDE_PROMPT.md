# CLAUDE_PROMPT: promptovi za Claude CLI

Kako se koristi:
1. Otvori terminal u korijenu repozitorija (`Smoke-Simulation-Using-a-grid-based-model`, folder u
   kojem je `CLAUDE.md`).
2. Pokreni `claude`.
3. Zalijepi **Prompt 1** (cio blok ispod). On NE piše kod, nego pravi detaljan plan implementacije
   i postavlja pitanja o otvorenim odlukama.
4. Kad plan odobriš, za svaki milestone zalijepi **Prompt 2** (zamijeni oznaku milestone-a).

Savjet: prvi prompt pokreni u "plan" režimu (`Shift+Tab` dok ne piše plan mode), da CLI ne može
mijenjati fajlove dok ne odobriš.

---

## Prompt 1: plan serijske verzije (zalijepiti u cijelosti)

```
Radimo studentski projekat za predmet Paralelni računarski sistemi: 3D simulacija dima na mreži
ćelija u C++-u. Trenutna faza je SAMO serijska verzija (jedna nit). Paralelizacija dolazi tek za
oko mjesec i po, ali kod mora biti napisan tako da je ne otežava.

PRVO PROČITAJ (redom):
1. CLAUDE.md
2. docs/plan/ARCHITECTURE.md, PLAN.md, MATH.md, VERIFICATION.md, CLI_OUTPUT.md, OPTIMIZATION.md, PERF_LOG.md
   (OPTIMIZATION.md se još ne primjenjuje, ali serijski kod mora biti pisan tako da ga ne koči;
   pogledaj i lab u docs/reference/PRS_Vektorizacija/)
3. docs/Simulacije dima, grid model, nasa zamisao.pdf (naš planski dokument)
4. literaturu u docs/: Stable Fluids, Real-Time Fluid Dynamics for Games, Visual Simulation of
   Smoke (najvažnije); ostale po potrebi
5. Solver/README.md i cijeli Solver/ (skelet već postoji i gradi se; plan se oslanja na njega i ne
   prepravlja strukturu bez razloga; ako nađeš grešku u njemu, prijavi je)
6. Renderer/README.md, Renderer/docs/architecture.md i Renderer/src/include/scene/Scene.h samo
   da razumiješ gdje se kasnije spaja naš kod. Renderer NE mijenjaš.

ZADATAK (NE PIŠI KOD U OVOM KORAKU):
Napravi detaljan plan implementacije serijske verzije i upiši ga u docs/plan/IMPLEMENTATION_PLAN.md.
Plan mora:
- slijediti arhitekturu iz ARCHITECTURE.md (Solver odvojen od Renderera, bez OpenGL-a u Solveru,
  veličina mreže promjenljiva, zadano 64^3, mogućnost 128^3 i izbora iz menija u rendereru),
- za svaki milestone M0 do M4 iz PLAN.md navesti (vizualizacija M-VIZ je zadnja i zasad se samo skicira): tačne fajlove koje treba napraviti, klase i
  potpise funkcija, redoslijed pisanja, koje testove (T1 do T10) pišemo uz koji dio i kako se
  provjerava da je milestone gotov,
- svaki novi pojam objasniti prije nego se koristi: šta je, odakle dolazi (koji rad i jednadžba),
  kako se povezuje s ostatkom. Ja sam student i želim da razumijem, ne samo da dobijem kod,
- označiti koji dijelovi pripadaju ulogama A, B i C da troje možemo raditi paralelno bez
  čekanja,
- navesti šta svaki član treba znati/pročitati prije svog dijela,
- ne izmišljati konstante ni pragove: gdje vrijednost mora doći iz rada ili iz mjerenja, to
  jasno označi (TODO(paper) ili TODO(measure)),
- predvidjeti gdje će se kasnije ubaciti OpenMP i SIMD (SlabRange, dvostruko baferovanje) bez
  da ih sada implementiraš.

PRIJE PISANJA PLANA postavi mi pitanja o otvorenim odlukama D1 do D7 iz PLAN.md (jedno po jedno
ili grupisano, s preporukom i kratkim objašnjenjem posljedica svake opcije). Kad odgovorim, upiši
odluke u PLAN.md i tek onda napiši IMPLEMENTATION_PLAN.md.

Ako u dokumentima nađeš nesaglasnost ili grešku (u formulama, redoslijedu koraka, arhitekturi),
nemoj tiho odstupati: reci mi šta si našao i predloži ispravku.

Na kraju mi daj kratak sažetak plana i čekaj moje odobrenje prije bilo kakvog pisanja koda.
```

---

## Prompt 2: implementacija jednog milestone-a (ponavljati po milestone-u)

Zamijeni `M1` odgovarajućom oznakom i ulogom (A, B ili C).

```
Implementiraj milestone M1 (uloga A) iz docs/plan/IMPLEMENTATION_PLAN.md.

Prije koda:
- reci mi u 5 do 10 rečenica šta ćemo napraviti i zašto, i koji pojmovi su novi,
- navedi fajlove koje ćeš napraviti ili izmijeniti.
Čekaj moje "ok".

Tokom rada:
- piši test zajedno s kodom (tabela testova u VERIFICATION.md),
- objasni svaki novi dio: šta je, odakle dolazi, kako se povezuje s ostalim,
- ne dodaji zavisnosti i ne diraj Renderer/ bez pitanja,
- ne optimizuj i ne paralelizuj.

Na kraju:
- pokreni izgradnju i testove i prijavi tačan ispis (i ako nešto ne prolazi),
- provjeri kriterij završetka milestone-a iz PLAN.md tačku po tačku,
- reci mi kako da to sam provjerim (koja komanda, šta treba vidjeti),
- predloži poruku za commit u formatu tip(opseg): opis, ali NE commituj dok ti ne kažem.
```

---

## Prompt 3: kad nešto ne radi

```
Ovo ne radi kako treba: [opiši šta vidiš, šta si očekivao, zalijepi ispis greške ili broj].
Prvo mi objasni šta misliš da je uzrok i zašto (uporedi s MATH.md i radovima u docs/), zatim
predloži najmanju izmjenu i kako ćemo provjeriti da je ispravna. Ne mijenjaj ništa dok ne kažem.
```
