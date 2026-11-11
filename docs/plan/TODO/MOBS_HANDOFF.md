# MOBS_HANDOFF: gdje smo stali (2026-11-11, kraj dana)

Privremeni fajl. Kad prođe tačka G3 i završi se P4.4, obrisati ga (sadržaj tada živi u OBSTACLES.md i PLAN.md).

## Kako nastaviti

U novoj sesiji reci Claudeu: **"Pročitaj docs/plan/TODO/MOBS_HANDOFF.md i nastavi gdje si stao."**

## Gdje smo

- Grana: `feature/obstacles`. Zadnji commit s kodom: `afaf239`. **Ništa nije pushano ni mergano.**
- Radno stablo je čisto, osim `Renderer/imgui.ini` i `docs/Sazetak_Simulacija_Dima.pdf` (nisu dio plana, ne dirati).
- Stojimo na tački **G3** (pregled prepreka, zaklanjanja i izvještaja). Čeka se Ermin.

Lanac grana (svaka polazi od prethodne, nijedna nije u `main`):
`docs/execution-plan` → `feature/m4d-profiling` → `feature/mviz` → `feature/obstacles`.

| Faza | Stanje | Commit |
| :--- | :--- | :--- |
| P1 (M4d): profil s brojačima, HTML, PROFILE.md | gotovo; G0: zasad se ništa ne optimizira | `90cdccc` |
| P2: konvergencija Jacobija; G1: Jacobi ostaje (D15) | gotovo | `604e3c2` |
| P3 (M-VIZ): dim u rendereru; G2 prošla | gotovo | `0f41561`, `278a0c4` |
| P4.1 do P4.3: solver s preprekama, testovi, OBSTACLES.md | gotovo | `88ad96c` |
| P4.4: mjerenje troška prepreka (protokol D10) | **nije urađeno** (vidi "Blokada") | |
| P4.5, P4.6: prepreke u rendereru, zaklanjanje, deka | gotovo | `afaf239` |
| P5: zatvaranje (profil s preprekama, M4c, nacrt izvještaja) | nije početo | |
| Zadnja stavka: ljepši HTML izvještaj profila, po uzoru na VTune | tek poslije svega | |

## Šta Ermin treba odlučiti na G3

1. **Odstupanje od plana u fizici.** EXECUTION_PLAN P4.2 kaže "brzina u solid ćelijama je nula". Urađeno je
   drugačije: u čvrstim ćelijama uz fluid brzina se **odbija** (normalna komponenta = −susjed, ostale = susjed),
   kao na zidovima kutije. Razlog je izmjeren: 32³, 600 koraka, kugla iznad izvora; dim pored kugle 12.6 s nulom,
   822.5 s odbijanjem. Objašnjenje: OBSTACLES.md, odjeljak 4. **Treba potvrda.**
2. **Mesh se ne crta kroz Assimp**, nego iz trouglova našeg parsera (isti vrhovi za crtanje i voxelizaciju).
   OBSTACLES.md, odjeljak 7. Treba potvrda ili zahtjev da se ipak ide kroz Assimp.
3. **Blokada (ispod).**

## Blokada: Windows Smart App Control

Poslije jedne od gradnji Windows je odbio pokrenuti `Solver/build/smoke_tests.exe` i `smoke_bench.exe`, a u
`Solver/build-prof` i `smoke_tests.exe`: *"An Application Control policy has blocked this file"*. **Nije
zaobilaženo.** Radili su: `Solver/build-noavx` (sve), `Solver/build-prof/smoke_bench.exe`, `Renderer/build/test-app/TestApp.exe`.

Zbog toga nije urađeno:
- **P4.4**: trošak prepreka po protokolu D10 (korak s praznom i s popunjenom maskom, unos u PERF_LOG.md i
  `perf/results.csv`). Mjeri se samo na običnoj Release gradnji `Solver/build`.
- Cijeli skup testova u AVX2 gradnji poslije zadnje izmjene (u njoj su mijenjane samo granice u testu).

Prvo sutra provjeriti da li blokada još vrijedi:
```
Solver\build\smoke_tests.exe
Solver\build\smoke_bench.exe --verify Solver\reference\serial_64_100steps_pc2.bin --grid 64 --steps 100
```
Ako prolazi, uraditi P4.4. Ako ne, Ermin odlučuje (pokrenuti sam, ili promijeniti postavku Windowsa).

## Šta jeste provjereno (2026-11-11)

- Bez AVX2 (`Solver/build-noavx`): 58/58 testova, `--verify` bit po bit isto kao `serial_64_100steps_pc2.bin`.
- S AVX2 jezgrima (`Solver/build-prof`): `--verify` bit po bit isto.
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

## Sljedeći koraci, redom

1. G3: Erminov pregled demoa (u panelu: Kugla, Kocka, Deka) i tri odluke iznad.
2. P4.4 kad blokada prođe: mjerenje troška prepreka, unos u PERF_LOG.md i `perf/results.csv` u istom commitu.
3. P5: ponovni profil s preprekama (uProf, isti postupak kao P1), M4c iz PLAN.md (pregled koda, nacrt izvještaja),
   označeni milestone-ovi.
4. Zadnja stavka: ljepši i jednostavniji HTML izvještaj profila, po uzoru na VTune (dogovoriti s Erminom šta tačno).
5. Push i merge u `main` samo kad Ermin zatraži. `CLAUDE.md` još opisuje stanje od prije M-VIZ ("Redoslijed: M4d,
   M-VIZ, M-OBS"); ažurirati ga kad se grane spoje.

## Gdje je šta objašnjeno

| Tema | Dokument |
| :--- | :--- |
| Profil, brojači, hipoteze H1 do H5, kandidati za optimizaciju | `docs/plan/PROFILE.md` |
| Dim u rendereru: dijelovi, prenos gustine, ray marching | `Integration/README.md` |
| Prepreke: voxelizacija, liste, fizika, ograničenja, testovi | `docs/plan/OBSTACLES.md` |
| Šta je mijenjano u Rendereru i šta smo o njemu naučili | `docs/plan/ARCHITECTURE.md`, odjeljak 6 |
| Stanje milestone-ova i odluke D1 do D17 | `docs/plan/PLAN.md` |
