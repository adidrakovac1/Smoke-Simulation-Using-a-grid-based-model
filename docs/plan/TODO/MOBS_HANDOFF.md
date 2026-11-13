# MOBS_HANDOFF: gdje smo stali (ažurirano 2026-11-13)

Privremeni fajl. Kad prođe tačka G3, obrisati ga (sadržaj tada živi u OBSTACLES.md, PERF_LOG.md i PLAN.md).

## Kako nastaviti

U novoj sesiji reci Claudeu: **"Pročitaj docs/plan/TODO/MOBS_HANDOFF.md i nastavi gdje si stao."**

## Gdje smo

- Grana: `feature/obstacles`. **Ništa nije pushano ni mergano.**
- Radno stablo je čisto, osim `Renderer/imgui.ini` i `docs/Sazetak_Simulacija_Dima.pdf` (nisu dio plana, ne dirati).
- **P4 je cijeli urađen** (2026-11-12 dovršen P4.4). **G3 je prošla 2026-11-12** (Ermin: "možeš nastaviti").
- **P5 je urađen u dijelu koji može uraditi izvršitelj** (2026-11-12): profil s preprekama, automatski pregled
  koda, test za `SlabRange` (62 testa), nacrt izvještaja (`docs/plan/REPORT_DRAFT.md`), usklađeni README fajlovi.
- **Zadnja stavka plana je urađena 2026-11-13**: HTML izvještaj profila po uzoru na VTune
  (`tools/uprof_report.py`, `perf/uprof/report.html`; opis u PROFILE.md, odjeljak 3.4). Čeka Erminov pregled.
- **Cijeli `EXECUTION_PLAN.md` je time izvršen.** Ostaju samo stvari koje čekaju Ermina ili tim (dolje).

Lanac grana (svaka polazi od prethodne, nijedna nije u `main`):
`docs/execution-plan` → `feature/m4d-profiling` → `feature/mviz` → `feature/obstacles`.

| Faza | Stanje | Commit |
| :--- | :--- | :--- |
| P1 (M4d): profil s brojačima, HTML, PROFILE.md | gotovo; G0: zasad se ništa ne optimizira | `90cdccc` |
| P2: konvergencija Jacobija; G1: Jacobi ostaje (D15) | gotovo | `604e3c2` |
| P3 (M-VIZ): dim u rendereru; G2 prošla | gotovo | `0f41561`, `278a0c4` |
| P4.1 do P4.3: solver s preprekama, testovi, OBSTACLES.md | gotovo | `88ad96c` |
| P4.4: mjerenje troška prepreka (protokol D10) | gotovo 2026-11-12 (PERF_LOG.md, odjeljak 9) | `5228eb2` |
| P4.5, P4.6: prepreke u rendereru, zaklanjanje, deka | gotovo | `afaf239` |
| P5a: profil s preprekama (uProf, 6 sesija, HTML, PROFILE.md odjeljak 6) | gotovo 2026-11-12 | `6fd9d21` |
| P5b: test za `SlabRange`, pregled koda (automatski dio) | gotovo 2026-11-12 | `5c90943` |
| P5c: nacrt izvještaja, README fajlovi, PLAN | gotovo 2026-11-12 | commit poslije `5c90943` |
| Zadnja stavka: HTML izvještaj profila s prikazima kao VTune | gotovo 2026-11-13; čeka Erminov pregled | commit poslije `1b19cf9` |

## Otvoreno za Ermina (ne blokira rad)

1. **Odstupanje od plana u fizici.** EXECUTION_PLAN P4.2 kaže "brzina u solid ćelijama je nula". Urađeno je
   drugačije: u čvrstim ćelijama uz fluid brzina se **odbija** (normalna komponenta = −susjed, ostale = susjed),
   kao na zidovima kutije. Razlog je izmjeren: 32³, 600 koraka, kugla iznad izvora; dim pored kugle 12.6 s nulom,
   822.5 s odbijanjem. Objašnjenje: OBSTACLES.md, odjeljak 4. **Treba potvrda.**
2. **Mesh se ne crta kroz Assimp**, nego iz trouglova našeg parsera (isti vrhovi za crtanje i voxelizaciju).
   OBSTACLES.md, odjeljak 7. Treba potvrda ili zahtjev da se ipak ide kroz Assimp.
3. **Pregled demoa rukom** (u panelu: Kugla, Kocka, Deka; kamera mišem; po želji vlastiti `.obj`). Do sada je sve
   provjereno samo automatskim pokretanjem.
4. **Da li ponoviti mjerenje troška na tihom računaru.** Mjerenje od 2026-11-12 je rađeno dok je računar bio u
   upotrebi; trošak prepreka je ispao ispod šuma (procjena za kuglu na 64³ oko 3.6%, šum 6 do 7%).
5. **Pregled novog HTML izvještaja** (`perf/uprof/report.html`, otvoriti dvoklikom). Popis dodatnih prikaza nije
   bio dogovoren, pa je uzet prijedlog iz ovog fajla: Sažetak, Hotspots, Mikroarhitektura, Keš i memorija,
   Instrukcije, Poređenje. Ako treba još nešto, dva prikaza traže novi rad s uProf-om: **flame graph / stablo
   poziva** (novo skupljanje sa stekom poziva) i **linije izvornog koda** (novi izvoz bez `--disasm-only`).

## Blokada Windows Smart App Control: prošla

2026-11-11 Windows je odbijao pokrenuti `Solver/build/smoke_tests.exe` i `smoke_bench.exe`. 2026-11-12 isti
programi se pokreću, i poslije nove gradnje. Ništa nije zaobilaženo niti mijenjano u postavkama. Ako se ponovi:
ne zaobilaziti, javiti Erminu.

## Šta je provjereno

2026-11-12, kraj dana: **62/62 testa** u gradnji s AVX2 (`Solver/build`) i bez AVX2 (`Solver/build-noavx`);
`--verify` bit po bit isto u obje, i u profilnoj gradnji (`Solver/build-prof`).

2026-11-12, AVX2 gradnja (`Solver/build`), prije dodavanja testa za `SlabRange`:
- 58/58 testova; `--verify` bit po bit isto kao `serial_64_100steps_pc2.bin`.
- Gradnja iz `278a0c4` (prije prepreka): `--verify` bit po bit isto.
- Trošak prepreka: dvije sesije po D10, unos u PERF_LOG.md (odjeljak 9) i 140 redova u `perf/results.csv`
  (`PC2-20261112-OBS-*`, `PC2-20261112-OBS64-*`).

2026-11-11:
- Bez AVX2 (`Solver/build-noavx`): 58/58 testova, `--verify` bit po bit isto.
- Hash gustine s preprekama (32³, 500 koraka) isti u obje gradnje: `6563899018697465459`.
- Renderer: tri postojeće scene iste prije i poslije izmjene dubinskog bafera (razlika samo brojač FPS-a).
- Slike: `docs/plan/img/mviz_64.png`, `mviz_128.png`, `mobs_sphere_330.png`, `mobs_sphere_700.png`,
  `mobs_cube_700.png`, `mobs_blanket_700.png`.

Nije provjereno rukom: dugmad u ImGui panelu, upravljanje kamerom mišem, vlastiti `.obj` fajl.

## Kako se gradi i pokreće

```
cmake -S Solver -B Solver/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build Solver/build --parallel

cmake -S Renderer -B Renderer/build -G "MinGW Makefiles" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build Renderer/build --parallel
cd Renderer
build\test-app\TestApp.exe
```

- `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` je obavezan na PC-2: bez njega Rendererov CMake pokušava osvježiti
  `stb` s interneta i pada na git konfliktu.
- Program se pokreće iz foldera `Renderer/`.
- Provjera bez ruku (program uradi korake, snimi prozor i zatvori se):
  `SMOKE_AUTOSHOT_STEPS=330`, `SMOKE_AUTOSHOT_FILE=C:\putanja\slika.png`, opcionalno
  `SMOKE_AUTOSHOT_GRID=128` i `SMOKE_AUTOSHOT_OBSTACLE=sphere|cube|blanket`.
- Slika bilo koje scene: `SMOKE_SCENE_SHOT_INDEX=n`, `SMOKE_SCENE_SHOT_FILE=...`.
- Korak s preprekom u mjerenju: `Solver\build\smoke_bench.exe --grid 64 --obstacle sphere` (ili `blanket`).

## Sljedeći koraci, redom

1. ~~HTML izvještaj profila s prikazima kao Intel VTune.~~ **Urađeno 2026-11-13.**
   - Erminove riječi (2026-11-12): "može HTML, ali želim prikaze kao što VTune ima, dakle cache misses, hotness,
     i šta već ne."
   - Urađeno iz postojećih 36 sesija (bez novog skupljanja, bez novih biblioteka): sesije istog posla spojene u
     14 "mjerenja"; kartice Sažetak, Hotspots (mapa faza → funkcija, grupisanje), Mikroarhitektura (CPI, grananja,
     frontend), Keš i memorija (L1 promašaji, punjenja L1 iz L2 / L3 / RAM-a, udio funkcije u svim punjenjima),
     Instrukcije (po adresi, toplotna skala po udjelu ili brojaču), Poređenje.
   - Nije urađeno jer podataka nema: stablo poziva i flame graph, Top-down podjela ciklusa, prikaz po objektima
     memorije, linije izvornog koda, vremenska osa. Isto piše u kartici "O mjerenju" i u PROFILE.md, odjeljak 3.4.
   - Provjera: sve kartice otvorene u headless Chromeu bez JS grešaka; brojevi u karticama se slažu s tabelama u
     PROFILE.md 4.5 (npr. Jacobi CPI 0.54 / 1.45, `setBoundary` punjenja iz RAM-a 52.7 PTI na 128³).
   - Fajl je 4.9 MiB (prije 2.6 MiB) jer sada nosi sve instrukcije s uzorcima, ne samo 40 najvrelijih po funkciji.
2. Ermin: pregled demoa rukom i potvrda dva odstupanja (lista "Otvoreno za Ermina" gore).
3. Tim: pregled koda među članovima i mjesta **[DOPUNITI]** u `docs/plan/REPORT_DRAFT.md`.
4. Push i merge u `main` samo kad Ermin zatraži. Lanac grana:
   `docs/execution-plan` → `feature/m4d-profiling` → `feature/mviz` → `feature/obstacles`.
   Pri spajanju ažurirati `CLAUDE.md`: još opisuje stanje od prije M-VIZ ("Redoslijed: M4d, M-VIZ, M-OBS") i
   navodi 48 testova (sada ih je 62).
5. Po želji: ponoviti mjerenje troška prepreka na tihom računaru (komande su na dnu PERF_LOG.md, odjeljak 9).

Paralelizacija ostaje izvan plana (D8) dok Ermin i asistent predmeta ne kažu drugačije.

## Gdje je šta objašnjeno

| Tema | Dokument |
| :--- | :--- |
| Profil, brojači, hipoteze H1 do H5, kandidati za optimizaciju | `docs/plan/PROFILE.md` |
| Dim u rendereru: dijelovi, prenos gustine, ray marching | `Integration/README.md` |
| Prepreke: voxelizacija, liste, fizika, ograničenja, testovi, trošak | `docs/plan/OBSTACLES.md` |
| Mjerenje troška prepreka (tabele) | `docs/plan/PERF_LOG.md`, odjeljak 9 |
| Šta je mijenjano u Rendereru i šta smo o njemu naučili | `docs/plan/ARCHITECTURE.md`, odjeljak 6 |
| Profil s preprekama | `docs/plan/PROFILE.md`, odjeljak 6 |
| Gdje se `SlabRange` smije dijeliti | `docs/plan/ARCHITECTURE.md`, odjeljak 5, pravilo 1 |
| Nacrt izvještaja | `docs/plan/REPORT_DRAFT.md` |
| Stanje milestone-ova i odluke D1 do D17 | `docs/plan/PLAN.md` |
