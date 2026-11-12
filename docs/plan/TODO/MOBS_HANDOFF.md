# MOBS_HANDOFF: gdje smo stali (ažurirano 2026-11-12)

Privremeni fajl. Kad prođe tačka G3, obrisati ga (sadržaj tada živi u OBSTACLES.md, PERF_LOG.md i PLAN.md).

## Kako nastaviti

U novoj sesiji reci Claudeu: **"Pročitaj docs/plan/TODO/MOBS_HANDOFF.md i nastavi gdje si stao."**

## Gdje smo

- Grana: `feature/obstacles`. **Ništa nije pushano ni mergano.**
- Radno stablo je čisto, osim `Renderer/imgui.ini` i `docs/Sazetak_Simulacija_Dima.pdf` (nisu dio plana, ne dirati).
- **P4 je cijeli urađen** (2026-11-12 dovršen P4.4). **G3 je prošla 2026-11-12** (Ermin: "možeš nastaviti").
- Radi se **P5**. Urađeno: profil s preprekama (PROFILE.md, odjeljak 6). Sljedeće: M4c (vidi dolje).

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
| P5a: profil s preprekama (uProf, 6 sesija, HTML, PROFILE.md odjeljak 6) | gotovo 2026-11-12 | commit poslije `5228eb2` |
| P5b: M4c (pregled koda, TODO-ovi, `SlabRange`, nacrt izvještaja), označeni milestone-ovi | **sljedeće** | |
| Zadnja stavka: ljepši HTML izvještaj profila, po uzoru na VTune | tek poslije svega | |

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

## Blokada Windows Smart App Control: prošla

2026-11-11 Windows je odbijao pokrenuti `Solver/build/smoke_tests.exe` i `smoke_bench.exe`. 2026-11-12 isti
programi se pokreću, i poslije nove gradnje. Ništa nije zaobilaženo niti mijenjano u postavkama. Ako se ponovi:
ne zaobilaziti, javiti Erminu.

## Šta je provjereno

2026-11-12, AVX2 gradnja (`Solver/build`):
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
Gradnja bez AVX2 nije ponovo pokrenuta 2026-11-12 (u solveru se ništa nije mijenjalo, samo `smoke_bench`).

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

1. G3: Erminov pregled demoa i odluke iznad.
2. P5: ponovni profil s preprekama (uProf, isti postupak kao P1), M4c iz PLAN.md (pregled koda, nacrt izvještaja),
   označeni milestone-ovi.
3. Zadnja stavka: ljepši i jednostavniji HTML izvještaj profila, po uzoru na VTune (dogovoriti s Erminom šta tačno).
4. Push i merge u `main` samo kad Ermin zatraži. `CLAUDE.md` još opisuje stanje od prije M-VIZ ("Redoslijed: M4d,
   M-VIZ, M-OBS") i broj testova 48 (sada ih je 58); ažurirati ga kad se grane spoje.

## Gdje je šta objašnjeno

| Tema | Dokument |
| :--- | :--- |
| Profil, brojači, hipoteze H1 do H5, kandidati za optimizaciju | `docs/plan/PROFILE.md` |
| Dim u rendereru: dijelovi, prenos gustine, ray marching | `Integration/README.md` |
| Prepreke: voxelizacija, liste, fizika, ograničenja, testovi, trošak | `docs/plan/OBSTACLES.md` |
| Mjerenje troška prepreka (tabele) | `docs/plan/PERF_LOG.md`, odjeljak 9 |
| Šta je mijenjano u Rendereru i šta smo o njemu naučili | `docs/plan/ARCHITECTURE.md`, odjeljak 6 |
| Stanje milestone-ova i odluke D1 do D17 | `docs/plan/PLAN.md` |
