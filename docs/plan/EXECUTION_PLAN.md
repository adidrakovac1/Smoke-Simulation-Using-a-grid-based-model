# EXECUTION_PLAN: plan rada za izvršitelja (Claude CLI na PC-2)

Odobren: **2026-11-11** (Ermin). Ovo je radni plan za sljedeću fazu: profiliranje serijske verzije,
vizualizacija dima u rendereru i prepreke (mesh `.obj`). Čita se uz [PLAN.md](PLAN.md),
[ARCHITECTURE.md](ARCHITECTURE.md), [MATH.md](MATH.md), [VERIFICATION.md](VERIFICATION.md),
[OPTIMIZATION.md](OPTIMIZATION.md) i [PERF_LOG.md](PERF_LOG.md).

---

## 0. Pravila za izvršitelja

1. **Ermin uči.** Za svaki dio koji praviš objasni (na bosanskom, u razgovoru i u dokumentu uz kod):
   (a) šta je to, (b) odakle dolazi (rad, jednadžba ili odluka), (c) kako se povezuje s ostalim dijelovima.
   Ovo je najvažnije pravilo, važnije od brzine rada.
2. **Autonomija.** Ovaj plan je odobren. Unutar njega radi bez traženja odobrenja za svaki korak, ali se
   **zaustavi na tačkama G0, G1, G2 i G3** (odjeljak 5) i čekaj Ermina.
3. **Git.** Radi na granama `feature/<naziv>`; commit je dozvoljen na kraju svakog zadatka (poruke
   `tip(opseg): opis`). Push, merge i bilo kakav rad na `main` samo kad Ermin to traži.
4. **Modeli.** Za teške probleme (fizika prepreka, promjena depth-a u Rendereru, rad sa simbolima u profileru)
   koristi najjači dostupan model, **Fable 5.2** (npr. kroz subagenta). Kad se njegov usage potroši, nastavi
   na **Sonnet 5.5, medium effort**. Rutinski rad (dokumenti, konverter, ImGui panel) može i na Sonnetu.
5. Pravila iz `CLAUDE.md` vrijede: Release gradnja, jedna izmjena po koraku, test uz kod, ne izmišljati
   brojeve (`TODO(measure)`), nove biblioteke samo uz pitanje, svaka izmjena brzine u `PERF_LOG.md` i
   `perf/results.csv`, odluke upisane u dokumente.
6. **Referenca se ne smije pokvariti.** Bez prepreka, `smoke_bench --verify` mora i dalje davati rezultat
   **bit po bit isti** kao `serial_64_100steps_pc2.bin`, u AVX2 gradnji i u gradnji bez AVX2.
7. Okruženje: PC-2 (Windows 11, MSYS2 UCRT64 g++, CMake MinGW Makefiles). Putanja bez razmaka.
   Mjerenja brzine su važeća samo na PC-2 (Ryzen 5 5500, Zen 3).
8. Prije svake faze kratko napiši šta ćeš uraditi i zašto; poslije svakog koraka reci kako se provjerava.
   Koristi listu zadataka da Ermin vidi napredak.

---

## 1. Stanje na početku (2026-11-11)

- Serijska optimizacija je gotova za opseg D11. Cijeli korak na PC-2, **S prema B0 / prema B1**:
  32³ 3.08x / 2.34x, 64³ 3.22x / 2.35x, 96³ 3.24x / 2.33x, 128³ 2.23x / 1.77x (PERF_LOG.md, odjeljak 7).
  Rezultat je bit po bit isti kao referenca, 48/48 testova.
- Na 128³ projekcija je oko 59% koraka i ograničena je memorijom (polja ne staju u L3 od 16 MiB).
- **Nalaz iz MATH.md:** preostala divergencija ne dolazi od spore konvergencije Jacobija (K = 5000 daje isto
  kao K = 1000), nego od granice projekcije na kolociranoj mreži. Bolji rješavač ubrzava, ali ne popravlja kvalitet.
  Mjereno samo na 64³; za 128³ nije mjereno (zadatak P2).
- Poznati nedostaci: kalibracija memorije daje ~107 GB/s (teorijski maksimum dvokanalnog DDR4 je ~51 GB/s;
  uzrok nije utvrđen), pa se E ne računa; 96³ je bučna; README i PLAN zaostaju za kodom.
- `Renderer/` je tuđi kod. Ima Assimp učitavanje modela, ImGui, HDR framebuffer; nema 3D teksture ni
  depth koji se može uzorkovati (ARCHITECTURE.md, odjeljak 6).

---

## 2. Odluke donesene 2026-11-11 (upisane i u PLAN.md)

| ID | Odluka |
| :--- | :--- |
| D13 | `Renderer/` se smije mijenjati kad je potrebno (prva potreba: depth u teksturu za zaklanjanje dima). Svaka izmjena se upisuje u ARCHITECTURE.md, odjeljak 6. |
| D14 | Redoslijed: **M4d** (profiling) pa **M-VIZ** (vizualizacija) pa **M-OBS** (prepreke). |
| D15 | Jacobi ostaje rješavač pritiska dok P2 ne pokaže da K = 40 na 128³ ne dostiže "pod" tačnosti; tek tada red-black Gauss-Seidel kao eksperiment (poredi se po vremenu do iste divergencije, ne po vremenu iteracije). Multigrid se ne radi. |
| D16 | Prepreke se rade kroz **rijetke liste graničnih ćelija**: vektorizirana jezgra (Jacobi, advekcija, sile) ostaju netaknuta. Vlastiti mali OBJ parser u `Solver/`, bez nove biblioteke. Bez prepreka nema nikakvog dodatnog posla po ćeliji. |
| D17 | Simulacija se korača jednom po frejmu u glavnoj niti (D8: bez niti). Prva verzija scene je jednostavna: dim u zatvorenoj kutiji (vidljiv okvir kutije, pod, izvor pri dnu), bez složene scene. |

---

## 3. Faze

### P0: čišćenje (mali zadaci)
1. **Kalibracija memorije** (`Solver/bench/bench_main.cpp`, `measureMemoryTriad`). Sadašnji triad računa isti
   rezultat u svih 6 prolaza (`b`, `c` se ne mijenjaju), pa ga kompajler može spojiti. Napravi da svaki prolaz
   zavisi od prethodnog (rotiraj uloge nizova ili pišeš u niz koji sljedeći prolaz čita), neka su nizovi najmanje
   nekoliko puta veći od L3, i provjeri `objdump -d` da su prolazi stvarno u kodu. Ermin treba reći brzinu RAM-a
   (`Get-CimInstance Win32_PhysicalMemory | Select Speed,ConfiguredClockSpeed`); teorijski maksimum se računa iz toga.
   **Gotovo kad:** rezultat nije veći od teorijskog maksimuma (ili je uzrok objašnjen i dokumentiran); tek tada se računa E.
2. **`smoke_bench --hwinfo`**: ispisuje procesor (CPUID), AVX2/FMA, kompajler, opcije gradnje. Tabela hardvera
   (Ryzen 5 5500, L1/L2/L3, RAM, tip i brzina memorije, Windows, kompajler) ide u `docs/plan/PROFILE.md`.
3. Ažuriraj `README.md` (status) i označi urađeno u PLAN.md (M4a, M4b, M4c gdje je tačno).

### P1 (M4d): AMD uProf i vizuelni izvještaj
Cilj: vidjeti gdje procesor čeka (keš promašaji, grananja, zastoji) i dobiti pregledan izvještaj, jer je GUI
alata loš. Zavisnosti među instrukcijama daju uglavnom **backend** zastoje; frontend zastoji su fetch/decode,
µop keš i pogrešna grananja. Mjeri se oboje.
1. **Instalacija.** Preuzmi AMD uProf sa službene AMD stranice (https://www.amd.com/en/developer/uprof.html);
   pokušaj prvo `winget search uprof`. Instalacija traži administratora i UAC (Ermin potvrđuje). Provjeri
   `AMDuProfCLI --version` i `AMDuProfCLI info --system`. Stranica navodi podršku za Windows 11 do 26H1, a
   Ermin ima build 10.0.26300: ako instalacija ili drajver za brojače ne rade, **stani i javi**.
   Koje konfiguracije postoje, pročitaj iz `AMDuProfCLI info --list ...` i `collect --help` (ne pretpostavljaj imena).
2. **Profilna gradnja:** `Solver/build-prof`, Release opcije plus `-g -fno-omit-frame-pointer`
   (`-g` ne mijenja generisani kod; ovaj build služi samo za nalaženje mjesta, S i Δ se mjere na običnom Releaseu).
   Provjeri da li uProf prikazuje imena funkcija iz g++ simbola. Ako ne, rezervni put: clang iz MSYS2 s
   `-gcodeview` i PDB; ako ni to, javi Erminu opcije (profiliranje na Linuxu).
3. **Radno opterećenje:** novi program `smoke_prof` (`Solver/apps/`): `--grid N --seconds S --stage
   all|jacobi|advect|forces`; zagrijava, pa vrti korak ustaljeno S sekundi, bez ispisa po koraku (da uzorkivač
   dobije dovoljno uzoraka). Gradi se uz ostalo.
4. **Skupljanje podataka** (64³ i 128³; po fazi): time-based (`tbp`), procjena mikroarhitekture (`assess`),
   po mogućnosti IBS (precizni keš promašaji i pogrešna grananja po instrukciji). Rezultati u
   `perf/uprof/<datum>_<oznaka>/`. Izvještaj generiši `AMDuProfCLI report ...` i **prvo pogledaj stvarni CSV
   izlaz** (nazive kolona, mjerne jedinice), pa tek onda piši konverter.
5. **Konverter** `tools/uprof_report.py` (ako Python nije na PC-2, pitaj Ermina; rezervno PowerShell ili C++).
   Pravi jedan **samostalan HTML** (CSS, JS i SVG unutra, bez interneta): zaglavlje s hardverom i opisom sesije;
   sortabilna i filtrirana tabela funkcija; jedinice metrika objašnjene ("pti" = na 1000 instrukcija);
   stubasti grafikoni za najvrelije funkcije; IPC, L1/L2/L3 promašaji, pogrešna grananja, zastoji; funkcije
   povezane s fazama (Jacobi, advekcija, sile, projekcija, rubni uslovi); **poređenje dvije sesije** (64³ i 128³,
   prije i poslije izmjene) s delta stupcem. Otvori ga u pregledniku i pokaži Erminu.
6. **`docs/plan/PROFILE.md`**: tabela hardvera, opis kako je mjereno, odgovori na hipoteze s dokazom:
   H1 Jacobi na 128³ je ograničen memorijom (L2/L3 promašaji); H2 Jacobi na 64³ ograničen dijeljenjem (`divps`);
   H3 advekcija troši vrijeme na oponašani gather; H4 grananja u vrućim jezgrima rijetko griješe;
   H5 frontend zastoja nema. Ne izmišljaj pragove; ako treba prag, `TODO(measure)`. Navedi kandidate za
   optimizaciju poredane po dokazu. **Ne implementiraj optimizacije** bez G0 i bez protokola iz OPTIMIZATION.md.

### P2: provjera Jacobija (bez novog rješavača)
Dodaj režim (u `smoke_cli` ili `smoke_bench`, npr. `--convergence`) koji za mreže 32³, 64³, 128³ i K = 10, 20,
40, 80, 160, 1000 poslije istog broja koraka ispisuje `max|div|` prije i poslije projekcije, rms, i ostatak
Poissonove jednačine `max|∇²p − div|` u zadnjem koraku, za ε = 5 i ε = 0. Tabelu upiši u MATH.md.
**Gotovo kad:** tabela postoji i na njoj piše odluka D15 (ostaje Jacobi, ili se radi red-black eksperiment).

### P3 (M-VIZ): integracija solvera i renderera
Prema ARCHITECTURE.md, odjeljci 1, 6 i 8. Solver ostaje bez OpenGL-a; spaja ih samo `Integration/`.
1. `Integration/` (CMake, `include/`, `src/`, `shaders/`): `SmokeScene`, `Texture3D`, `VolumeRaymarchPass`,
   `SmokeUI`. Jedine izmjene u `Renderer/test-app`: `CMakeLists.txt` i `main.cpp` (registracija scene).
2. **Scena (prva verzija):** vidljiv okvir kutije, pod, izvor dima pri dnu kutije, orbitna kamera koju Renderer
   već ima; jedna instanca `SmokeSolver`. Kutija je nevidljiva granica simulacije (ARCHITECTURE.md, odjeljak 8).
   Provjeri koja je osa "gore" u Rendereru (uzgon djeluje duž lokalnog +y kutije).
3. **Prenos gustoće:** 3D tekstura (jedan kanal, float), linearno uzorkovanje, `glTexSubImage3D` svaki frejm;
   `GL_UNPACK_ROW_LENGTH` i `GL_UNPACK_IMAGE_HEIGHT` iz `DensityView` (strideJ, strideK) ili kompaktna kopija
   unutrašnjosti; objasni izbor Erminu.
4. **Volumni prolaz** u `OnPostRender`: zraka kroz kutiju (presjek zrake i kutije), uzorkovanje s fiksnim brojem
   koraka (podesiv), Beer-Lambert prozirnost, crtanje u HDR FBO s blendom. Referenca: [Crane07]. Opcionalno jedna
   zraka prema svjetlu za jednostavno zasjenjenje.
5. **ImGui panel:** veličina mreže (32/64/96/128) s dugmetom Apply (`resize()`), ε, uzgon α i β, K, dt, izvor
   (pozicija, radijus, jačina, temperatura), pauza, jedan korak, reset, gustina i broj koraka prolaza, ispis
   ms po fazi iz `StepTimings` i FPS.
6. Bez zaklanjanja objektima u ovoj fazi (dolazi u M-OBS).
**Gotovo kad:** dim se diže kroz kutiju i vidljivo se uvija u realnom vremenu na 64³; veličina mreže se mijenja
iz menija; na 128³ radi (FPS se ispiše, bez praga). Snimi screenshot za dokumentaciju.

### P4 (M-OBS): prepreke i mesh
Detalji dizajna i objašnjenja za Ermina ide u novi `docs/plan/OBSTACLES.md` (bosanski: šta, odakle, kako se spaja).
1. **Solver (čisti C++, bez Renderera):**
   - Parser `.obj` (v, f, triangulacija poligona, negativni indeksi, bez materijala) → lista trouglova.
   - `ObstacleGrid`: tip ćelije **Fluid / Solid / Porous** (+ propusnost).
   - Voxelizacija: presjek trougla i ćelije (SAT) daje ljusku; za zatvorene meshove unutrašnjost se označava
     poplavom od vana. Načini objekta: puni, ljuska (tanke stvari), porozni (npr. deka).
   - Javni API: `SmokeSolver::setObstacles(...)`, `clearObstacles()`; liste graničnih ćelija se grade jednom.
2. **Fizika (D16):**
   - Poslije svake Jacobi iteracije: pritisak se iz susjednih fluidnih ćelija kopira u granične solid ćelije
     (isti princip kao `setBoundary` za zidove kutije; Neumann uslov). Jacobi jezgro se ne mijenja.
   - Brzina u solid ćelijama je nula (poslije sila, advekcije i projekcije).
   - Prije advekcije gustina se kopira u granične solid ćelije (dim klizi uz prepreku umjesto da nestane),
     poslije advekcije solid ćelije se čiste.
   - Porozne ćelije su fluid za pritisak; dodaje se otpor `u *= 1/(1 + dt·σ)` i opcionalno filtriranje gustine
     `ρ *= 1/(1 + dt·φ)`, samo po listi poroznih ćelija.
   - Ako nema prepreka, put kroz kod je jedan `if` po fazi izvan petlji, i rezultat je bit po bit isti (pravilo 6).
   - Poznata slabost: ljuska debela jednu ćeliju kopira pritisak s obje strane; traži se debljina ≥ 2 ćelije ili
     poseban test.
3. **Testovi (uz kod, doctest):** volumen voxelizirane kocke i kugle prema analitičkom (tolerancija izmjerena,
   `TODO(measure)`); poplava; nema gustine u solid ćelijama poslije N koraka; divergencija uz prepreku;
   dim se odbija od kugle (težište se ne probija); prolazi kroz porozni sloj usporen; 500 koraka bez NaN;
   AVX2 i skalarno bit po bit isto s preprekama; bez prepreka `--verify` bit po bit isto.
4. **Mjerenje troška** po protokolu D10: korak s praznom i s popunjenom maskom, upis u PERF_LOG.md.
5. **Integration:** ImGui "Učitaj .obj" (putanja, skala, pozicija, rotacija u kutiji, tip: puni / ljuska /
   porozni), isti fajl se crta kroz Rendererov Assimp učitavač, isti transform za mesh i za voxelizaciju.
   Test meshevi (generiši skriptom, bez preuzimanja): kocka, kugla, ravna "deka" (mreža trouglova) u
   `Integration/assets/`.
6. **Renderer (D13):** depth u `HdrFramebuffer` iz renderbuffera u teksturu; u volumnom shaderu ograniči zraku
   na dubinu scene (objekti zaklanjaju dim). Dekа se crta u zasebnom prozirnom prolazu poslije volumena;
   upiši u ARCHITECTURE.md, odjeljak 6, šta je izmijenjeno i zašto.
**Gotovo kad:** dim se vidno odbija od kugle/kocke, prolazi kroz deku usporen, objekt zaklanja dim iza sebe,
testovi prolaze, a bez prepreka referenca je bit po bit ista.

### P5: zatvaranje
Ponovni profil s preprekama (uProf, isti postupak kao P1) i poređenje u HTML izvještaju; PERF_LOG i results.csv;
M4c iz PLAN.md (pregled koda, nacrt izvještaja: algoritam, tačnost, tabela S/Δ/E, profil); odluke i izmjerene
vrijednosti upisane u dokumente; označeni milestone-ovi. Paralelizacija ostaje izvan plana (D8) dok Ermin i
asistent predmeta ne kažu drugačije.

**Zadnja stavka plana (dodao Ermin 2026-11-11), radi se tek poslije svega iznad, kao zadnji korak prije
paralelizacije:** prepraviti HTML izvještaj profila (`tools/uprof_report.py`, `perf/uprof/report.html`) da ima
ljepši i jednostavniji grafički prikaz, po uzoru na Intel VTune. Šta tačno znači "kao VTune" (koji prikazi)
dogovara se s Erminom kad dođe na red; do tada se izvještaj ne dotjeruje.
**Dogovor 2026-11-12 (Ermin):** ostaje HTML, s prikazima kakve ima VTune: hotness (gdje ide vrijeme), cache misses
i ostali njegovi prikazi. Radi se u narednoj sesiji; razrada i ograničenja podataka su u
[TODO/MOBS_HANDOFF.md](TODO/MOBS_HANDOFF.md), "Sljedeći koraci", tačka 1.
**Urađeno 2026-11-13:** izvještaj ima kartice Sažetak, Hotspots, Mikroarhitektura, Keš i memorija, Instrukcije i
Poređenje (PROFILE.md, odjeljak 3.4). Koje dodatne prikaze uzeti Ermin nije precizirao ("i šta već ne"), pa je
uzet popis iz handoff fajla; čeka njegov pregled.

---

## 4. Radni redoslijed i grane

`feature/m4d-profiling` (P0, P1, P2) → `feature/mviz` (P3) → `feature/obstacles` (P4, P5).
Prvo commituj ove dokumente na `docs/execution-plan`.

## 5. Tačke zaustavljanja (čekaj Ermina)

- **G0** poslije P1: Ermin i izvršitelj pregledaju HTML izvještaj i PROFILE.md; odlučuje se šta (ako išta) optimizirati.
- **G1** poslije P2: odluka D15 (Jacobi ili red-black eksperiment).
- **G2** poslije P3: Ermin gleda demo; ispravke prije prepreka.
- **G3** poslije P4: pregled prepreka, zaklanjanja i izvještaja.
Zaustavi se i prije svega što traži instalaciju drajvera, administratorska prava, novu biblioteku ili brisanje.

## 6. Rizici

| Rizik | Odgovor |
| :--- | :--- |
| uProf drajver ne radi na Windows buildu 10.0.26300 | javi odmah; rezervno profiliranje na Linuxu ili samo tbp bez brojača |
| uProf ne prikazuje imena iz g++ DWARF simbola | clang + PDB; ako ne, javi opcije |
| Brojevi iz uProf-a ne slažu se s `smoke_demo` | uProf služi za pronalaženje uzroka; zaključci o brzini ostaju na S/Δ iz D10 protokola |
| Ghost-kopija pritiska pogrešna uz tanke prepreke | minimalna debljina 2 ćelije; test s pločom; dokumentuj |
| Promjena depth-a u Rendereru pokvari druge scene | provjeriti barem dvije postojeće scene prije i poslije |
| 128³ nije interaktivan (korak je skup) | očekivano (D17, D8); ispiši FPS, preporuči 64³ za demo |
