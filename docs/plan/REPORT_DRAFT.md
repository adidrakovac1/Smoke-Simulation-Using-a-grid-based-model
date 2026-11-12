# REPORT_DRAFT: nacrt izvještaja o serijskoj fazi

Nacrt za M4c (PLAN.md) i P5 (EXECUTION_PLAN.md), napisan 2026-11-12. **Ovo je skelet s brojevima, ne gotov
tekst izvještaja.** Svaki broj je prepisan iz dokumenta navedenog uz njega; ako se dokument promijeni, mijenja se
i ovdje. Ništa nije mjereno posebno za ovaj nacrt. Mjesta koja tim treba dopuniti označena su s **[DOPUNITI]**.

Predmet: Paralelni računarski sistemi, ETF Sarajevo. Tim: Ermin Hadžić, Ajdin Đinalić, Adi Drakovac.

---

## 1. Zadatak i opseg

- 3D simulacija dima na mreži ćelija (Eulerov pristup) u C++20, odvojena od renderera, pa ubrzana.
- **Ovaj izvještaj pokriva samo jednu nit** (odluka D8): ispravna serijska verzija, njena optimizacija
  (vektorizacija i fuzija jezgara), profil s hardverskim brojačima, prikaz u rendereru i prepreke.
- OpenMP niti i GPU **nisu urađeni**; odjeljak 9 kaže šta je za njih pripremljeno.

**[DOPUNITI]** rokovi i zahtjevi predmeta, podjela posla među članovima (PLAN.md, odjeljak 1, još nije upisana).

## 2. Algoritam

Izvor: MATH.md. Jedan korak, redom:

| # | Faza | Šta radi | Odakle |
| ---: | :--- | :--- | :--- |
| 1 | Izvor | `ρ += dt·rate`, `T += dt·rate` u kugli pri dnu | [Stam03] `add_source` |
| 2 | Sile | uzgon `v += dt·(−α·ρ + β·(T − T_amb))`; vorticity confinement `u += dt·ε·(N × ω)` | [Fedkiw01] jed. 8 i 9 do 11 |
| 3 | Advekcija brzine | semi-Lagrangian: `q_novo(x) = q_staro(x − dt·u(x))`, trilinearna interpolacija | [Stam99], [Stam03] `advect` |
| 4 | Projekcija | divergencija, K Jacobi iteracija za `∇²p = div`, `u −= ∇p` | [Stam03] `project` |
| 5 | Advekcija gustine i temperature | isto kao 3, popravljenom brzinom | [Stam99] |

Izbori koje treba navesti u izvještaju (svi su odluke iz PLAN.md, odjeljak 3):

- Sve veličine su u središtu ćelije (kolocirana mreža), jedinice ćelije `h = 1` (D4), ghost sloj od jedne ćelije
  za rubne uslove (D2), zatvorena kutija (D3), polje temperature (D1).
- Zadano: `dt = 1/60`, K = 40, `α = 0.05`, `β = 2`, `ε = 5` (D9). Parametri su podešeni gledanjem na 64³
  (MATH.md, odjeljak 3), nisu fizičke konstante.
- Rješavač pritiska je Jacobi s dvije kopije polja (D15): sporije konvergira od Gauss-Seidela, ali nijedna ćelija
  ne zavisi od redoslijeda obrade.

## 3. Implementacija

Izvor: ARCHITECTURE.md.

- **Tri sloja, jedan smjer zavisnosti.** `Solver/` (naš kod, samo standardna biblioteka), `Renderer/` (tuđi
  OpenGL renderer), `Integration/` (jedino mjesto koje poznaje oba). Solver se gradi, testira i mjeri bez
  grafičke kartice.
- **Podaci.** Svako polje je poseban niz `float` ("structure of arrays"), `i` je najbrži indeks, niz je poravnat
  na 64 B, a red zaokružen na 8 floatova. 17 polja: 64³ oko 17 MB, 128³ oko 136 MB.
- **Faze** su slobodne funkcije u `smoke::serial`, primaju polja i raspon slojeva `SlabRange`. Nema globalnog
  stanja ni slučajnih brojeva, pa je rezultat determinističan.
- **Javni interfejs** je jedna klasa, `SmokeSolver` (`step`, `resize`, `params`, `density`, `diagnostics`,
  `lastTimings`, `setObstacles`).
- Programi: `smoke_cli` (simulacija i slike presjeka), `smoke_tests`, `smoke_bench` (mjerenje, referenca,
  kalibracija, konvergencija), `smoke_prof` (opterećenje za profiler), `smoke_lab` i `smoke_demo` (poređenje
  verzija jezgara).

## 4. Tačnost

Izvori: VERIFICATION.md, MATH.md odjeljak 5, OBSTACLES.md odjeljak 6.

- **62 testa** (doctest), prolaze u gradnji s AVX2 jezgrima i bez njih. Tabela T1 do T10 je u VERIFICATION.md;
  uz to 10 testova prepreka i 4 testa raspona slojeva.
- **Referentni rezultat.** Gustina poslije 100 koraka na 64³ snimljena je prije optimizacije
  (`serial_64_100steps_pc2.bin`). Poslije **svake** izmjene u M4, i poslije dodavanja prepreka,
  `smoke_bench --verify` daje **bit po bit isti** rezultat. Zato se sva ubrzanja u odjeljku 6 odnose na isti račun.
- **Referenca zavisi od računara i kompajlera.** Isti kod na Apple M4 (AppleClang) i na Ryzenu (g++) razlikuje
  se relativno 1.4·10⁻⁴; vjerovatan uzrok (nije dokazano) je spajanje množenja i sabiranja u FMA na ARM-u.
- **Divergencija poslije projekcije ne ide u nulu.** Uz K = 5000 Poissonova jednačina je riješena (ostatak
  0.0002 na 64³), a `max|div|` ostaje 1.87 uz ε = 5. Uzrok je kolocirana mreža s centralnim razlikama, ne spor
  Jacobi. Tabela konvergencije za 32³, 64³ i 128³ i K = 10 do 1000 je u MATH.md, odjeljak 5.
- **Masa nije sačuvana tačno** (semi-Lagrangian): 7.3% u 100 koraka u testu T10.

## 5. Kako je mjereno

Izvori: OPTIMIZATION.md odjeljci 4 i 5, PROFILE.md odjeljak 1.

- **Računar (PC-2):** AMD Ryzen 5 5500 (Zen 3, 6 jezgara), L2 512 KiB po jezgru, L3 16 MiB, 32 GiB DDR4
  dvokanalno na 2400 MT/s, Windows 11, g++ 16.2.0, Release `-O3 -DNDEBUG`, bez `-march=native` i bez `-ffast-math`.
  AVX2 samo u tri fajla, uz provjeru procesora u toku rada.
- **Protokol D10:** osnova i kandidat u istoj sesiji, naizmjenično, najmanje 3 runde po 100 koraka (10
  zagrijavanja). Glavni broj je medijan medijana rundi. Šum je raspon medijana rundi osnove. Izmjena se priznaje
  samo ako je razlika veća od šuma **i** ista u svakoj rundi.
- **Metrike:** ubrzanje `S = T_osnova / T_nova`, delta `Δ`, efikasnost memorije `E` (postignuta propusnost
  jezgra prema izmjerenoj propusnosti računara). Apsolutna vremena se ne navode kao zaključak.
- **Dvije osnove:** B0 (isti kod, vektorizacija kompajlera isključena) i B1 (zadane optimizacije kompajlera).
- Svako mjerenje, i ono koje nije pomoglo, je u PERF_LOG.md i `perf/results.csv` (820 redova).

## 6. Rezultati optimizacije

Izvor: PERF_LOG.md, odjeljak 7 (PC-2, 2026-11-09). Ćelija: **S prema B0 / S prema B1**.

| Faza | 32³ | 64³ | 96³ | 128³ |
| :--- | ---: | ---: | ---: | ---: |
| sile (uzgon, vorticity) | 3.91x / 3.68x | 3.64x / 3.47x | 4.70x / 3.00x | 2.24x / 2.15x |
| advekcija brzine | 2.80x / 2.81x | 2.95x / 2.96x | 3.11x / 2.92x | 2.89x / 2.88x |
| projekcija | 3.32x / 1.66x | 3.48x / 1.63x | 3.03x / 1.54x | 1.88x / 1.13x |
| advekcija skalara | 2.63x / 2.65x | 2.81x / 2.81x | 2.55x / 2.57x | 2.77x / 2.77x |
| **cijeli korak** | **3.08x / 2.34x** | **3.22x / 2.35x** | **3.24x / 2.33x** | **2.23x / 1.77x** |

Šta je donijelo ubrzanje (unosi M4-001 do M4-020):

| Tehnika | Gdje | Ishod |
| :--- | :--- | :--- |
| Uklanjanje grananja (maske, clamp bez `if`) | vorticity confinement, advekcija | zadržano; preduslov da kompajler vektorizira |
| `restrict` na jezgrima po redu | projekcija, advekcija, sile | zadržano; samo za sebe bez ubrzanja, ali omogućava vektorizaciju |
| 32-bitni indeks i red izdvojen u funkciju | advekcija | zadržano; petlja se tek tada vektorizira |
| AVX2 po fajlu s provjerom procesora | Jacobi, advekcija, sile | zadržano |
| Fuzija jezgara (jedan prolaz za više polja) | advekcija brzine, gustina + temperatura | zadržano (jedina tehnika izvan laba) |
| `-fno-math-errno` za jedan fajl (D12) | sile | zadržano; preduslov da se `sqrt` vektorizira |
| Blokiranje Jacobija 4 × 8 | Jacobi | **odbačeno**: 9 do 14% sporije (mjereno na PC-1, M4-004) |
| Ručne AVX2 intrinsics | Jacobi | **nije uzeto**: isto kao ono što kompajler napravi uz `-mavx2` |
| `#pragma omp simd` | projekcija | **nije primijenjeno**: isti vektorski kod kao bez nje |

**Efikasnost memorije (PERF_LOG.md, odjeljak 8).** Propusnost računara (triad) je 17.9 GB/s, uz teorijskih
38.4 GB/s. Jacobi iteracija: E oko 2 na 32³ do 96³ (polja su u kešu), a **E ≈ 0.94 na 128³**. Na 128³ Jacobi
dakle radi na granici propusnosti memorije, i zato je tamo ubrzanje projekcije najmanje (1.13x prema B1).

Stvari koje u izvještaju treba reći otvoreno:

- Brojevi vrijede za PC-2. Na PC-1 (Apple M4) konačna verzija nije ponovo mjerena.
- Mreža 96³ je na PC-2 bučna u svim sesijama; uzrok nije istražen.
- Opseg je skraćen odlukom D11: nisu rađeni ručni gather, AVX2 za divergenciju i gradijent, ni poređenje
  poravnato / neporavnato.

## 7. Profil

Izvor: PROFILE.md (AMD uProf 6.0, vremensko uzorkovanje i hardverski brojači; IBS nije bio dostupan).

Udio faza u cijelom koraku poslije optimizacije:

| Faza | 64³ | 128³ |
| :--- | ---: | ---: |
| Jacobi | 35.2% | 49.4% |
| Advekcija | 43.4% | 27.0% |
| Sile | 8.7% | 9.8% |
| Rubni uslovi (`setBoundary`) | 7.5% | 8.5% |
| Projekcija bez Jacobija | 4.1% | 4.6% |

| Hipoteza | Odgovor |
| :--- | :--- |
| H1: Jacobi na 128³ je ograničen memorijom | podržana: CPI 0.54 na 64³ prema 1.45 na 128³ uz iste instrukcije; E ≈ 0.94; 2.03x više vremena po ćeliji |
| H2: Jacobi na 64³ je ograničen dijeljenjem | nije dokazano ni oboreno |
| H3: advekcija troši vrijeme na oponašani gather | djelimično: oko 36% uzoraka; najviše je ipak račun u registrima |
| H4: grananja rijetko griješe | podržana: 0.07 do 1.25% pogrešnih |
| H5: frontend zastoja nema | podržana u okviru mjerenog |

Nalazi koji nisu bili među hipotezama: `setBoundary` je 7.5 do 8.5% koraka (zove se poslije svake Jacobi
iteracije); AVX2 petlje čitaju blokove pomjerene 4 B od poravnanja, pa svako drugo čitanje prelazi granicu linije
keša (trošak nije izmjeren). Kandidati za dalju optimizaciju su u PROFILE.md, odjeljak 5; **nijedan nije
implementiran** (tačka G0).

## 8. Vizualizacija i prepreke

Izvori: Integration/README.md, OBSTACLES.md, PERF_LOG.md odjeljak 9, PROFILE.md odjeljak 6.

- **Prikaz.** Gustina se svaki frejm šalje u 3D teksturu direktno iz solverovog niza; dim se crta ray marchingom
  kroz kutiju s Beer-Lambertovom prozirnošću ([Crane07]). Na PC-2 (RTX 4060 Ti): 64³ radi 60 FPS (ograničeno
  vsync-om), 128³ oko 11 FPS, jer se simulacija korača u glavnoj niti (D17). Slike: `docs/plan/img/`.
- **Prepreke.** Mesh (`.obj`) se pretvara u ćelije (presjek trougla i kocke, pa poplava izvana), a u koraku se
  primjenjuje kroz **rijetke liste graničnih ćelija** (D16): vektorizirana jezgra ostaju netaknuta. Na površini
  prepreke vrijede isti uslovi kao na zidu kutije; porozne ćelije dobijaju otpor `u ← u / (1 + dt·σ)`.
- **Trošak prepreka (D10, 2026-11-12).** Bez prepreka trošak nije mjerljiv i rezultat je bit po bit isti. S kuglom
  ili dekom trošak cijelog koraka je ispod šuma mjerenja (procjena za kuglu na 64³ oko 3.6%, šum 6 do 7%).
- **Profil s preprekama.** Prepreke su nova faza od 2.2 do 2.5% koraka za kuglu i 0.1% za deku; oko 60% toga je
  prenos pritiska u čvrste ćelije poslije svake Jacobi iteracije. CPI ostalih jezgara se ne mijenja.

**[DOPUNITI]** slike za izvještaj i, po želji, kratak snimak demoa. Demo još nije pregledan rukom (PLAN.md, M-OBS).

## 9. Šta je pripremljeno za paralelizaciju (nije urađena)

Izvori: ARCHITECTURE.md odjeljak 5, MATH.md odjeljak 8, `Solver/tests/test_slab_range.cpp`.

- **Jezgra su nezavisna po slojevima.** Advekcija i Jacobi čitaju staro polje i pišu novo. Test poziva svako
  jezgro u komadima po `z` i dobija bit po bit isti rezultat kao jednim pozivom.
- **Granica koju treba znati:** složene funkcije faza (`project`, `advectVelocity`, `advectScalars`,
  `applyVorticityConfinement`) rade i posao nad cijelim poljem (zamjena bafera, `setBoundary`), pa se raspon
  dijeli unutar njih. `setBoundary` i petlje po listama prepreka nemaju raspon.
- **Računski primjer za Amdahlov zakon (nije mjerenje).** Po profilu na 128³ (PROFILE.md, odjeljak 6.2, bez
  prepreke) jezgra koja primaju raspon nose oko 89.5% koraka (Jacobi 51.9%, advekcija 24.1%, sile 9.2%, ostatak
  projekcije 4.3%), a `setBoundary` 9.8%. Uz `f = 0.895` i 6 niti formula daje najviše `S = 1 / (0.105 + 0.895/6)
  ≈ 3.9`. Stvarno ubrzanje će vjerovatno biti manje: Jacobi na 128³ već koristi 94% propusnosti memorije jedne
  niti (E ≈ 0.94), a memoriju dijele sve niti. Koliko manje, **nije izmjereno**.

## 10. Ograničenja i šta nije izmjereno

- Jedna nit; nema OpenMP-a ni GPU-a (D8).
- Sva mjerenja brzine konačne verzije su s jednog računara (PC-2).
- Trošak prepreka nije izmjeren na tihom računaru; 96³ je bučna.
- H2 nije razdvojena; trošak čitanja preko granice linije keša nije izmjeren; nijedan kandidat iz profila nema
  izmjereno ubrzanje.
- Divergencija ima "pod" koji bolji rješavač ne uklanja (kolocirana mreža); MAC mreža nije rađena.
- Parametri izgleda dima (`α`, `β`, `ε`, `σ`) su podešeni okom na 64³.
- Pregled koda među članovima tima nije urađen.

## 11. Literatura

[Stam99] Stable Fluids. [Stam03] Real-Time Fluid Dynamics for Games. [Fedkiw01] Visual Simulation of Smoke.
[Crane07] GPU Gems 3, poglavlje 30. [Amdahl67]. Akenine-Möller (2001), presjek trougla i kocke. PDF-ovi su u `docs/`.
**[DOPUNITI]** pune bibliografske reference u formatu koji predmet traži.
