# PROFILE: hardver i profil serijske verzije (M4d)

Ovaj dokument odgovara na pitanje **gdje procesor troši vrijeme i zašto** (keš promašaji, grananja, zastoji),
a ne koliko je nešto brže. Zaključci o brzini ostaju na S i Δ iz [PERF_LOG.md](PERF_LOG.md) (protokol D10).
Radni plan: [EXECUTION_PLAN.md](EXECUTION_PLAN.md), faze P0 i P1.

Stanje (2026-11-11): P0 i P1 su urađeni s **vremenskim uzorkovanjem (TBP)**. Hardverski brojači nisu dostupni
dok je uključen WSL2 / Hyper-V (odjeljak 3.3), pa su hipoteze H2, H4 i H5 otvorene.
HTML izvještaj: `perf/uprof/2026-11-11_tbp/report.html`.

---

## 1. Hardver i softver (PC-2)

| Šta | Vrijednost | Odakle je podatak |
| :--- | :--- | :--- |
| Procesor | AMD Ryzen 5 5500 (Zen 3, "Cezanne"), porodica 25 (0x19), model 80 (0x50), stepping 0 | `smoke_bench --hwinfo` (CPUID) |
| Jezgara / niti | 6 / 12 (SMT) | `Win32_Processor`, CPUID |
| Nazivni takt | 3600 MHz | `Win32_Processor.MaxClockSpeed` |
| Skupovi instrukcija | SSE4.2 da, AVX da, AVX2 da, FMA da, AVX-512 ne | `smoke_bench --hwinfo` |
| L1 podaci / instrukcije | 32 KiB / 32 KiB po jezgru, 8 puteva, linija 64 B | CPUID 0x8000001D |
| L2 | 512 KiB po jezgru (ukupno 6 × 512 KiB), 8 puteva | CPUID 0x8000001D |
| L3 | 16 MiB, zajednički za svih 6 jezgara, 16 puteva | CPUID 0x8000001D |
| RAM | 32 GiB: 2 × 16 GiB DDR4, Kingston KF3200C16D4/16GX, po jedan modul u kanalu A i B (dvokanalno) | `Win32_PhysicalMemory` |
| Brzina RAM-a | **2400 MT/s** (podešeno; moduli su predviđeni za 3200 uz XMP profil, koji nije uključen) | `Win32_PhysicalMemory.ConfiguredClockSpeed` |
| Matična ploča / BIOS | MSI B450M PRO-VDH MAX (MS-7A38), BIOS B.I0 (2023-04-27) | `Win32_BaseBoard`, `Win32_BIOS` |
| Operativni sistem | Windows 11 Pro 26H2, build 10.0.26300, 64-bit | `Win32_OperatingSystem` |
| Plan napajanja | High performance | `powercfg /getactivescheme` |
| Kompajler | g++ 16.2.0 (MSYS2 UCRT64, Rev4), CMake 4.5.0-rc3, MinGW Makefiles | `g++ --version`, `cmake --version` |
| Opcije gradnje | Release `-O3 -DNDEBUG`; po fajlu: `Projection_avx2.cpp -mavx2`, `Advection_avx2.cpp -mavx2 -fvect-cost-model=unlimited`, `Forces_avx2.cpp -mavx2 -fno-math-errno`, `Forces.cpp -fno-math-errno` | `smoke_bench --hwinfo` |

**Šta ove veličine znače za naš solver.** Jedno polje (`float`, s ghost slojem i poravnatim redovima) zauzima
0.18 MiB na 32³, 1.20 MiB na 64³, 3.81 MiB na 96³ i 8.77 MiB na 128³. Jacobi iteracija radi nad tri polja
(stari pritisak, divergencija, novi pritisak):

| Mreža | Tri polja Jacobija | Gdje staje |
| :--- | ---: | :--- |
| 32³ | 0.53 MiB | L2 (512 KiB) na granici, sigurno u L3 |
| 64³ | 3.6 MiB | L3 |
| 96³ | 11.4 MiB | L3 (16 MiB), ali ga većim dijelom puni |
| 128³ | 26.3 MiB | **ne staje u L3**, svaka iteracija ide u RAM |

Veličine polja su izračunate iz dimenzija (`(nx+2)` zaokruženo na 8, puta `(ny+2)(nz+2)`, puta 4 B), nisu mjerene.

### Kako se ponovo dobije ova tabela

```
Solver\build\smoke_bench.exe --hwinfo
Get-CimInstance Win32_PhysicalMemory | Select BankLabel,Capacity,Speed,ConfiguredClockSpeed,PartNumber
Get-CimInstance Win32_Processor | Select Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed
powercfg /getactivescheme
```

`--hwinfo` čita procesor instrukcijom **CPUID**: program upiše broj "lista" u registar EAX, a procesor u
EAX, EBX, ECX i EDX vrati podatke o sebi (ime, porodica, podržane instrukcije, keševi). Tip i brzinu RAM-a
program ne može pročitati; to zna samo BIOS (SMBIOS tabela), a Windows je izlaže kroz `Win32_PhysicalMemory`.

---

## 2. Propusnost memorije i efikasnost E

### 2.1 Teorijski maksimum

Jedan DDR4 kanal prenosi 8 bajta po prenosu. Na 2400 MT/s to je 2400·10⁶ × 8 B = 19.2 GB/s po kanalu, pa je
za dva kanala **38.4 GB/s**. (Uz XMP na 3200 MT/s bilo bi 51.2 GB/s; taj broj je ranije stajao u PERF_LOG-u
kao pretpostavka.) Stvarni programi dostižu manje od teorijskog maksimuma.

### 2.2 Greška u staroj kalibraciji i ispravka (2026-11-11)

`smoke_bench --calibrate` mjeri "triad" (jezgro iz testa STREAM): `a[i] = b[i] + s·c[i]` nad nizovima mnogo
većim od keša.

- **Šta je bilo pogrešno.** Stari kod je isti triad izvršavao 6 puta, a `b` i `c` se nisu mijenjali. Svih 6
  prolaza upisuje iste vrijednosti u `a`, pa je kompajler zakonito ostavio **samo jedan prolaz** (u asembleru
  je između dva očitanja sata bila jedna petlja), a program je računao bajte za 6. Rezultat je bio 6 puta
  prevelik: 88 do 107 GB/s.
- **Ispravka.** Tri niza sada rotiraju uloge: svaki prolaz čita niz koji je prethodni prolaz upisao, pa se
  nijedan ne može izbaciti. Jezgro je zasebna funkcija `triadPass` koja se ne ugrađuje (noinline).
- **Provjera u binarnom fajlu.** `objdump -d Solver/build/smoke_bench.exe` pokazuje funkciju `triadPass`
  (petlja `movups, movups, mulps, addps, movups`) i petlju u `main` koja je poziva 6 puta (`call triadPass`,
  `sub $1, %r9d`, `jne`), uz zamjenu tri pokazivača između poziva.
- **Rezultat poslije ispravke** (5 pokretanja, svako je medijan 5 mjerenja po 6 prolaza):
  17.13, 18.49, 17.89, 18.07, 16.96 GB/s; **medijan 17.9 GB/s**, raspon 1.5 GB/s.

Dva načina brojanja bajta, oba se ispisuju:

| Ispis | Šta broji | Vrijednost (medijan) |
| :--- | :--- | ---: |
| `memory_triad_GB_s` | 2 čitanja + 1 upis po elementu (dogovor iz testa STREAM) | 17.9 GB/s |
| `memory_triad_bus_GB_s` | 3 čitanja + 1 upis: prije običnog upisa procesor prvo učita liniju keša u koju piše ("write allocate") | 23.8 GB/s |

Oba su ispod teorijskih 38.4 GB/s (47% odnosno 62%), što je uobičajeno za jednu nit. Faktor 4/3 je model
(obični upisi, bez "non-temporal" instrukcija; u asembleru je upis `movups`, ne `movntps`), nije posebno mjeren.

### 2.3 Efikasnost memorije E za Jacobi

`E = postignuta GB/s jezgra / izmjerena GB/s računara` (OPTIMIZATION.md, odjeljak 5.1). Jedno orijentaciono
mjerenje 2026-11-11 (`smoke_bench`, 100 koraka + 10 zagrijavanja, K = 40; triad u istoj sesiji 18.29 GB/s;
računar nije bio posebno umiren, pa ovo **nije** mjerenje po protokolu D10):

| Mreža | Jacobi iteracija, medijan (ms) | GB/s (12 B po ćeliji) | **E** | GB/s (32 B po ćeliji) |
| :--- | ---: | ---: | ---: | ---: |
| 32³ | 0.00966 | 40.7 | 2.23 | 108.5 |
| 64³ | 0.0738 | 42.6 | 2.33 | 113.7 |
| 96³ | 0.287 | 37.0 | 2.02 | 98.7 |
| 128³ | 1.465 | 17.2 | **0.94** | 45.8 |

- **12 B po ćeliji** je isti dogovor kao kod triada: po iteraciji se iz memorije jednom čita stari pritisak,
  jednom divergencija i jednom piše novi pritisak. Šest susjeda pritiska su ista tri sloja koja su već u kešu.
- **32 B po ćeliji** je model iz VERIFICATION.md, odjeljak 4.4 (7 čitanja + 1 upis, bez keša). On broji svako
  čitanje susjeda kao promet memorije, pa daje prevelike brojeve; naveden je radi veze sa starijim unosima.
- Vrijeme Jacobi iteracije uključuje i `setBoundary` poslije iteracije.

**Tumačenje.** Na 32³ do 96³ je E oko 2: polja su u kešu (tabela u odjeljku 1), pa jezgro radi brže nego što
RAM može isporučiti i nije ograničeno memorijom. Na 128³ je **E ≈ 0.94**: Jacobi prenosi podatke gotovo
tačno onoliko brzo koliko ovaj računar može (triad), dakle ograničen je propusnošću memorije. To je prvi
dokaz za hipotezu H1 (odjeljak 4); brojači iz uProf-a je trebaju potvrditi ili oboriti.

---

## 3. Kako je profilirano (P1)

### 3.1 Profilna gradnja (urađeno 2026-11-11)

```
cmake -S Solver -B Solver/build-prof -G "MinGW Makefiles" -DSMOKE_PROFILE_BUILD=ON
cmake --build Solver/build-prof --parallel
```

To su iste Release opcije (`-O3 -DNDEBUG` i opcije po fajlu) plus:

- `-g`: simboli za debug (DWARF). Po njima profiler iz adrese instrukcije nalazi ime funkcije i liniju koda.
  Sam `-g` ne mijenja generisane instrukcije.
- `-fno-omit-frame-pointer`: registar RBP ostaje pokazivač okvira funkcije, pa profiler može pratiti ko je koga
  pozvao. Cijena je jedan registar manje za kompajler, pa kod **nije** potpuno isti kao u običnoj gradnji.

Zato ova gradnja služi samo za **nalaženje mjesta**; S i Δ se uvijek mjere na običnom Releaseu (`Solver/build`).
Provjera: 48/48 testova i `smoke_bench --verify` bit po bit isto i u profilnoj gradnji.

### 3.2 Radno opterećenje: `smoke_prof` (urađeno 2026-11-11)

Profiler koji uzorkuje prekida program mnogo puta u sekundi i bilježi koja se instrukcija izvršavala. Da u
vrućim jezgrima bude dovoljno uzoraka, `smoke_prof` (`Solver/apps/smoke_prof/main.cpp`) prvo razvije stvarno
strujanje (100 cijelih koraka, kao referentni scenarij), a zatim zadani broj sekundi ponavlja jednu vrstu
posla, bez ispisa u toku rada. Poziva iste funkcije faza kao `SmokeSolver::step` (`Solver/src/stages/Stages.h`).

| `--stage` | Šta se ponavlja | Jedno "ponavljanje" |
| :--- | :--- | :--- |
| `all` | cijeli korak, istim redom kao `SmokeSolver::step`; svakih 300 koraka kreće od prazne kutije (`--reset-every`), da strujanje ostane kao u podešenom scenariju | jedan korak |
| `jacobi` | rješavanje pritiska kao u `project()`: `p = 0`, pa K iteracija, svaka sa `setBoundary`; divergencija je izračunata jednom | jedna Jacobi iteracija |
| `advect` | obje advekcije jednog koraka (u, v, w, pa gustina i temperatura), uvijek iz istih razvijenih polja u rezervna polja | pet advektiranih polja |
| `forces` | uzgon i vorticity confinement; prije svakog poziva brzina se vraća na razvijenu (`memcpy`), inače bi rasla bez granice | jedan poziv `applyForces` |

Primjer: `Solver\build-prof\smoke_prof.exe --grid 128 --seconds 20 --stage jacobi`. Na kraju ispisuje trajanje,
broj ponavljanja i ms po ponavljanju. Kratka proba na 64³ (2 s, obična gradnja): cijeli korak 8.3 ms, Jacobi
iteracija 0.080 ms, advekcija 3.5 ms, sile 0.88 ms (uključuje `memcpy`); to su probe da program radi, ne mjerenja.

### 3.3 AMD uProf: šta radi, a šta ne (2026-11-11)

- **Verzija:** AMD uProf 6.0.1520, `C:\Program Files\AMD\AMDuProf\bin\AMDuProfCLI.exe`. Prepoznaje procesor
  (porodica 0x19, model 0x50) i ima 6 brojača po jezgru (`AMDuProfCLI info --system`).
- **Simboli:** imena funkcija iz g++ simbola (DWARF) se prikazuju ispravno, uključujući funkcije iz anonimnog
  namespacea (`advectRow3`, `vorticityRow`). Rezervni put s clangom i PDB-om nije bio potreban.
- **Radi: vremensko uzorkovanje (TBP).** Svake 1 ms profiler zapiše koja se instrukcija izvršavala. Daje vrijeme
  po funkciji i raspodjelu uzoraka po instrukciji.
- **Ne radi: hardverski brojači (EBP) i IBS.** `collect --config assess_ext` vraća
  *"Only timer-based profiling (TBP) is available when WSL2 is enabled."* Windows na PC-2 radi pod Hyper-V
  hipervizorom (zbog WSL2), a hipervizor ne daje gostu pristup brojačima. `info --system` zato prikazuje
  `IBS: No` i `Hypervisor Enabled: Yes`. Posljedica: **keš promašaji, pogrešna grananja, IPC i zastoji nisu
  izmjereni.** Da bi se izmjerili, treba isključiti WSL2 / Virtual Machine Platform / Hyper-V (administrator i
  restart; odluka je Erminova) i ponoviti `perf/uprof/collect.sh ebp assess_ext`.
- **Pomoć u konzoli:** `AMDuProfCLI <komanda> --help` čeka pritisak tipke, pa se u skripti ne može čitati.
  Isti tekst je u `C:\Program Files\AMD\AMDuProf\bin\Help\text\*.txt`.

### 3.4 Skupljanje i izvještaj

```
perf/uprof/collect.sh tbp                 # 64³ i 128³, faze all / jacobi / advect / forces, po 20 s
python tools/uprof_report.py perf/uprof/2026-11-11_tbp -o perf/uprof/2026-11-11_tbp/report.html
```

- `collect.sh` za svaku sesiju pokreće `smoke_prof` pod uProf-om (`--start-delay` preskače 100 koraka razvoja
  strujanja), pa pravi `report.csv` (tabela funkcija) i `detail.csv` (uzorci po instrukciji). U
  `session_info.txt` upisuje ispis `smoke_bench --hwinfo`, module RAM-a i verziju profilera.
- `tools/uprof_report.py` (samo standardna biblioteka Pythona) čita te CSV fajlove i pravi **jedan samostalan
  HTML**: hardver i sesije, udio faza po sesiji, sortabilna i filtrirana tabela funkcija, najvrelije
  instrukcije izabrane funkcije s podjelom po vrsti instrukcije, i poređenje dvije sesije s delta stupcem.
  Funkcije su fazama pridružene po imenu (tabela `PHASES` u skripti).
- Sirovi fajlovi uProf-a (`.prd`, `.db`) se ne commituju (`.gitignore`); CSV, `workload.txt` i HTML da.

**Oprez pri čitanju instrukcija ("skid").** Uzorak se često pripiše instrukciji *poslije* one koja je stvarno
spora. Visok postotak na jednoj instrukciji znači "ovdje ili neposredno prije". TBP ne kaže *zašto* je mjesto sporo.

---

## 4. Nalazi i hipoteze (TBP, 2026-11-11)

Sesije: `perf/uprof/2026-11-11_tbp/` (profilna gradnja, 20 s po sesiji, oko 19 000 do 29 000 uzoraka svaka).
Vremena iz `smoke_prof` su iz profilne gradnje pod profilerom; služe za odnose unutar ove tabele, ne za S.

### 4.1 Gdje ide vrijeme cijelog koraka (`--stage all`)

| Faza | 64³ | 128³ |
| :--- | ---: | ---: |
| Jacobi (`jacobiSlabAvx2`) | 35.2% | 49.4% |
| Advekcija (`advectRow3` + `advectRow2`) | 43.4% | 27.0% |
| Sile (vorticity confinement, uzgon) | 8.7% | 9.8% |
| Rubni uslovi (`setBoundary`) | 7.5% | 8.5% |
| Projekcija bez Jacobija (divergencija, gradijent) | 4.1% | 4.6% |
| Kopiranje i punjenje (`fill`, `clear`) | 1.0% | 0.7% |

### 4.2 Vrijeme po ćeliji (iz `workload.txt`)

| Posao | 64³ | 128³ | 128³ / 64³ |
| :--- | ---: | ---: | ---: |
| Jacobi iteracija (sa `setBoundary`) | 0.285 ns po ćeliji | 0.579 ns | 2.03x |
| Advekcija (5 polja) | 13.5 ns po ćeliji | 12.0 ns | 0.89x |
| Sile (uključuje `memcpy` brzine) | 4.20 ns po ćeliji | 5.75 ns | 1.37x |

Da jezgro ne zavisi od veličine podataka, vrijeme po ćeliji bi bilo isto. Jacobi je na 128³ dvostruko sporiji po
ćeliji, advekcija nije sporija.

### 4.3 Hipoteze

| ID | Hipoteza | Odgovor | Dokaz |
| :--- | :--- | :--- | :--- |
| H1 | Jacobi na 128³ je ograničen memorijom | **Podržana posredno; brojači nisu izmjereni.** | Vrijeme po ćeliji je 2.03x veće nego na 64³ (4.2); E ≈ 0.94 (2.3); tri polja imaju 26.3 MiB prema 16 MiB L3 (odjeljak 1). Na 128³ se uzorci šire na instrukcije koje čitaju iz memorije (`vsubps (div)` 16%, `add` iza njega 23%), dok su na 64³ skupljeni na jednom mjestu. L2/L3 promašaji nisu izmjereni. |
| H2 | Jacobi na 64³ je ograničen dijeljenjem (`vdivps`) | **Nije dokazano ni oboreno.** | Na 64³ 53% uzoraka sesije pada na upis `vmovups` odmah iza `vdivps`, a samo 4.7% na `vdivps`. Zbog skida to odgovara H2, ali isto bi izgledalo da je spor sam upis. Razdvojio bi ih brojač ili eksperiment (kandidat 3 dolje). |
| H3 | Advekcija troši vrijeme na oponašani gather | **Djelimično.** | U sesijama `advect` (64³ / 128³): slaganje vektora iz pojedinačnih vrijednosti (`vinsertps`, `vmovlhps`, `vextract...`) 25.9% / 27.1%, pojedinačna čitanja `vmovss` 10.0% / 10.4%; zajedno oko 36%. Najveća vrsta je ipak račun u registrima (težine i interpolacija) 41.6% / 40.8%. Još 14 do 15% su čitanja iz memorije, među njima i vraćanje vrijednosti sa steka (`mov 0x270(%rbp)`): kompajleru ponestaje registara. Advekcija nije ograničena memorijom (4.2). |
| H4 | Grananja u vrućim jezgrima rijetko griješe | **Nije izmjereno** (treba brojač pogrešnih grananja). | Posredno: na instrukcije grananja pada najviše 0.5% uzoraka u svakoj sesiji, a vruće petlje nemaju grananja osim povratka na početak petlje. To nije mjerenje promašaja. |
| H5 | Frontend zastoja nema | **Nije izmjereno** (trebaju brojači). | Posredno: vruća petlja Jacobija ima 12 instrukcija. To nije dokaz. |

### 4.4 Nalaz koji nije bio među hipotezama: `setBoundary`

`setBoundary` je **7.5% (64³) i 8.5% (128³) cijelog koraka**, i oko 9 do 10% sesije `jacobi`, jer se poziva
poslije svake Jacobi iteracije (40 puta po koraku). Najviše uzoraka je u skalarnoj petlji s `mulss` (množenje
predznakom, po jedna vrijednost). Za pritisak je predznak uvijek +1, pa je to množenje jedinicom.

---

## 5. Kandidati za optimizaciju, poredani po dokazu

Ništa od ovoga **nije implementirano**. Odluka je na tački G0, a svaka izmjena ide po protokolu iz
OPTIMIZATION.md (hipoteza, tačnost, mjerenje po D10, unos u PERF_LOG.md).

| # | Kandidat | Dokaz | Očekivani domet | Mijenja rezultat? |
| ---: | :--- | :--- | :--- | :--- |
| 1 | `setBoundary`: bez množenja za skalarna polja, petlje po plohama koje se vektoriziraju (nivo 1: T10, T12) | 7.5 do 8.5% cijelog koraka, skalarni `mulss` (4.4) | najviše taj udio; koliko stvarno, nije izmjereno | ne mora (kopija umjesto množenja s 1 je bit po bit ista) |
| 2 | Jacobi na 128³: manje prometa memorije (npr. upis bez "write allocate", nivo 2: fino podešavanje memorije) | E ≈ 0.94, 2.03x po ćeliji (H1) | nije izmjereno; na mrežama koje staju u keš može biti sporije, pa bi išlo samo za velike mreže | ne |
| 3 | Jacobi: množenje s 1/6 umjesto `vdivps` | H2 nije razdvojena; eksperiment bi je razdvojio | nije izmjereno | **da** (drugačije zaokruživanje; D11 to isključuje dok tim ne odluči) |
| 4 | Advekcija: pravi `vgatherdps` ili manje pritiska na registre | oko 36% uzoraka advekcije u slaganju vektora i pojedinačnim čitanjima (H3) | nije izmjereno; M4-014 bilježi da ručni gather na Zen 3 nije provjeren | ne |
| 5 | Sile na 128³ | 1.37x po ćeliji prema 64³ (uključuje `memcpy` iz `smoke_prof`) | slab dokaz | ne |

**Šta nedostaje za čvrste zaključke:** hardverski brojači (odjeljak 3.3). Bez njih su H1 i H3 podržane posredno,
a H2, H4 i H5 otvorene.
