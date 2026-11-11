# PROFILE: hardver i profil serijske verzije (M4d)

Ovaj dokument odgovara na pitanje **gdje procesor troši vrijeme i zašto** (keš promašaji, grananja, zastoji),
a ne koliko je nešto brže. Zaključci o brzini ostaju na S i Δ iz [PERF_LOG.md](PERF_LOG.md) (protokol D10).
Radni plan: [EXECUTION_PLAN.md](EXECUTION_PLAN.md), faze P0 i P1.

Stanje (2026-11-11): P0 i P1 su urađeni, prvo s **vremenskim uzorkovanjem (TBP)**, a poslije isključenja WSL2 i
s **hardverskim brojačima (EBP)**. IBS nije dostupan (odjeljak 3.3). Odgovori na hipoteze su u odjeljku 4.3;
H2 ostaje otvorena. HTML izvještaj sa svim sesijama: `perf/uprof/report.html`.

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
- **Prvi pokušaj: hardverski brojači (EBP) nisu radili.** `collect --config assess_ext` je vraćao
  *"Only timer-based profiling (TBP) is available when WSL2 is enabled."* Zato je prvi krug urađen samo s TBP-om.
- **Poslije isključenja WSL2 i restarta (2026-11-11, navečer): EBP radi.** Ermin je isključio Windows komponente
  WSL, Virtual Machine Platform, Hyper-V i Hypervisor Platform. `info --system` sada daje `Core PMC: Yes (6
  counters per core)` i `EBP Supported: Yes`, a `collect --config assess_ext` skuplja brojače bez upozorenja.
- **Hipervizor je i dalje aktivan** (`Hypervisor Enabled: Yes`, u izvještaju `Virtual Machine: Yes`). Drži ga
  VBS (Virtualization Based Security, `Win32_DeviceGuard.VirtualizationBasedSecurityStatus = 2`), sigurnosna
  funkcija Windowsa koja sama pokreće Hyper-V hipervizor. Windows tada radi kao korijenska particija iznad
  hipervizora; brojači po jezgru su dostupni, ali **IBS nije** (`IBS: No`). IBS (Instruction Based Sampling) je
  AMD-ov precizniji način uzorkovanja koji uzorak veže za tačnu instrukciju, bez skida. Za njega bi trebalo
  ugasiti i VBS (administrator i restart; odluka je Erminova). Nije urađeno.
- **Samo korisnički način rada.** Bez administratorskih prava uProf skuplja uzorke samo iz korisničkog koda
  (`User: True, OS: False` u tabeli događaja). Solver cijeli radi u korisničkom načinu.
- **Šta `assess_ext` mjeri.** 19 događaja na 6 brojača, pa ih uProf mijenja u toku rada (multipleksiranje):
  cikluse, izvršene instrukcije, grananja i pogrešna grananja, pristupe L1 kešu, punjenja L1 iz L2 / L3 / RAM-a,
  neporavnata čitanja, sukobe upisa i čitanja. Iz njih računa izvedene kolone (CPI, "PTI" = na 1000 instrukcija).
  Brojači za L3 i memorijski kontroler (`L3 PMC`, `UMC PMC`) nisu dostupni, pa L3 promašaje vidimo samo posredno,
  kroz punjenja L1 iz RAM-a.
- **Profiler usporava program.** Pod `assess_ext` je `smoke_prof` sporiji nego pod TBP-om (npr. cijeli korak na
  64³: 10.9 ms prema 7.7 ms). Zato iz ovih sesija čitamo **omjere** (CPI, postoci, PTI), a ne vremena.
- **Pomoć u konzoli:** `AMDuProfCLI <komanda> --help` čeka pritisak tipke, pa se u skripti ne može čitati.
  Isti tekst je u `C:\Program Files\AMD\AMDuProf\bin\Help\text\*.txt`.

### 3.4 Skupljanje i izvještaj

```
perf/uprof/collect.sh tbp                 # 64³ i 128³, faze all / jacobi / advect / forces, po 20 s
perf/uprof/collect.sh ebp assess_ext      # isto, s hardverskim brojačima
perf/uprof/collect.sh inst inst_access 10 # keš instrukcija i ITLB (frontend), po 10 s
python tools/uprof_report.py perf/uprof/2026-11-11_tbp perf/uprof/2026-11-11_ebp perf/uprof/2026-11-11_inst -o perf/uprof/report.html
python tools/uprof_report.py perf/uprof/2026-11-11_ebp --summary    # tekst: faze, brojači i instrukcije po funkciji
```

- `collect.sh` za svaku sesiju pokreće `smoke_prof` pod uProf-om (`--start-delay` preskače 100 koraka razvoja
  strujanja), pa pravi `report.csv` (tabela funkcija) i `detail.csv` (uzorci po instrukciji). U
  `session_info.txt` upisuje ispis `smoke_bench --hwinfo`, module RAM-a i verziju profilera.
- `tools/uprof_report.py` (samo standardna biblioteka Pythona) čita te CSV fajlove i pravi **jedan samostalan
  HTML**: hardver i sesije, udio faza po sesiji, sortabilna i filtrirana tabela funkcija, najvrelije
  instrukcije izabrane funkcije s podjelom po vrsti instrukcije, i poređenje dvije sesije s delta stupcem.
  Funkcije su fazama pridružene po imenu (tabela `PHASES` u skripti). Kolone metrika čita po imenu, pa iste
  funkcije obrađuju TBP i sesije s brojačima; kod brojača prikazuje i formulu svake izvedene kolone (iz odjeljka
  `DERIVED METRICS` u CSV-u) i brojače po instrukciji.
- Sirovi fajlovi uProf-a (`.prd`, `.db`) se ne commituju (`.gitignore`); CSV, `workload.txt` i HTML da.

**Oprez pri čitanju instrukcija ("skid").** Uzorak se često pripiše instrukciji *poslije* one koja je stvarno
spora. Visok postotak na jednoj instrukciji znači "ovdje ili neposredno prije". TBP ne kaže *zašto* je mjesto sporo.

---

## 4. Nalazi i hipoteze (2026-11-11)

Sesije: `perf/uprof/2026-11-11_tbp/` (vremensko uzorkovanje, oko 19 000 do 29 000 uzoraka svaka),
`perf/uprof/2026-11-11_ebp/` (brojači, `assess_ext`) i `perf/uprof/2026-11-11_inst/` (brojači, `inst_access`).
Sve su iz profilne gradnje, 20 s po sesiji (`inst`: 10 s). Vremena iz `smoke_prof` su iz profilne gradnje pod
profilerom; služe za odnose unutar tabela, ne za S. Odjeljci 4.1 i 4.2 su iz TBP sesija, 4.5 do 4.7 iz brojača.

### 4.1 Gdje ide vrijeme cijelog koraka (`--stage all`)

| Faza | 64³ | 128³ |
| :--- | ---: | ---: |
| Jacobi (`jacobiSlabAvx2`) | 35.2% | 49.4% |
| Advekcija (`advectRow3` + `advectRow2`) | 43.4% | 27.0% |
| Sile (vorticity confinement, uzgon) | 8.7% | 9.8% |
| Rubni uslovi (`setBoundary`) | 7.5% | 8.5% |
| Projekcija bez Jacobija (divergencija, gradijent) | 4.1% | 4.6% |
| Kopiranje i punjenje (`fill`, `clear`) | 1.0% | 0.7% |

Sesije s brojačima daju istu sliku, ovaj put kao udio ciklusa: na 64³ Jacobi 35.3%, advekcija 42.4%, sile 8.5%,
`setBoundary` 7.9%; na 128³ Jacobi 52.5%, advekcija 23.7%, sile 9.9%, `setBoundary` 8.4%. Dva nezavisna načina
mjerenja se slažu na nekoliko procentnih poena.

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
| H1 | Jacobi na 128³ je ograničen memorijom | **Podržana**, s tri nezavisna dokaza. Direktan broj L3 promašaja nije dostupan (nema `L3 PMC`). | Vrijeme po ćeliji je 2.03x veće nego na 64³ (4.2); E ≈ 0.94 (2.3); tri polja imaju 26.3 MiB prema 16 MiB L3 (odjeljak 1). Brojači (4.5): iste instrukcije, CPI 0.54 na 64³ prema 1.45 na 128³, grananja ne griješe, a punjenja L1 iz RAM-a rastu s 0.00 na 1.50 PTI. |
| H2 | Jacobi na 64³ je ograničen dijeljenjem (`vdivps`) | **Nije dokazano ni oboreno.** | Na 64³ 53 do 55% uzoraka sesije pada na upis `vmovups` odmah iza `vdivps` (TBP i brojači isto), a samo 4 do 5% na `vdivps`. Zbog skida to odgovara H2, ali isto bi izgledalo da je spor sam upis. `assess_ext` nema brojač zauzetosti djelitelja, a pojavilo se i drugo objašnjenje: taj upis svaki drugi put prelazi granicu linije keša (4.6). Dodatni znak opreza: na tu istu instrukciju pada i 54% uzoraka brojača *izvršenih instrukcija*, iako se svaka od 12 instrukcija petlje izvrši jednako često. Dakle raspodjela po instrukcijama u ovoj petlji je posljedica načina uzorkovanja, ne mjera troška. Razdvojio bi ih eksperiment (kandidati 2 i 4 dolje) ili IBS. |
| H3 | Advekcija troši vrijeme na oponašani gather | **Djelimično.** | U sesijama `advect` (64³ / 128³): slaganje vektora iz pojedinačnih vrijednosti (`vinsertps`, `vmovlhps`, `vextract...`) 25.9% / 27.1%, pojedinačna čitanja `vmovss` 10.0% / 10.4%; zajedno oko 36%. Najveća vrsta je ipak račun u registrima (težine i interpolacija) 41.6% / 40.8%. Još 14 do 15% su čitanja iz memorije, među njima i vraćanje vrijednosti sa steka (`mov 0x270(%rbp)`): kompajleru ponestaje registara. Advekcija nije ograničena memorijom (4.2); brojači to potvrđuju: CPI je isti na 64³ i 128³ (0.38 do 0.44) i gotovo nema punjenja L1 izvan L2 (4.5). |
| H4 | Grananja u vrućim jezgrima rijetko griješe | **Podržana.** | Brojači (4.5): pogrešno je predviđeno 0.07 do 0.59% grananja u Jacobiju, `setBoundary` i silama, a 0.9 do 1.25% u advekciji, gdje grananja gotovo i nema (2 do 4 na 1000 instrukcija). Prag za "rijetko" nismo zadavali; ovo su izmjereni brojevi. |
| H5 | Frontend zastoja nema | **Podržana u okviru onoga što je mjereno.** | Brojači `inst_access` (4.7): promašaji keša instrukcija ispod 1.2 na 1000 instrukcija, promašaji ITLB-a praktično 0, op cache promašuje 1 do 2.4% pristupa u Jacobiju, silama i `setBoundary`. Izuzetak je advekcija (3.6 do 12.6%), ali ona ima najniži CPI. Zastoji pri otpremanju µop-ova nisu mjereni. |

### 4.4 Nalaz koji nije bio među hipotezama: `setBoundary`

`setBoundary` je **7.5% (64³) i 8.5% (128³) cijelog koraka**, i oko 9 do 10% sesije `jacobi`, jer se poziva
poslije svake Jacobi iteracije (40 puta po koraku). Najviše uzoraka je u skalarnoj petlji s `mulss` (množenje
predznakom, po jedna vrijednost). Za pritisak je predznak uvijek +1, pa je to množenje jedinicom.

### 4.5 Šta kažu hardverski brojači (`assess_ext`)

**Kako čitati kolone.** Brojač je registar u procesoru koji broji jedan događaj (ciklus, izvršenu instrukciju,
promašaj keša). uProf uzme uzorak svaki put kad brojač odbroji zadani broj događaja, pa iz omjera uzoraka računa:

- **CPI** (ciklusa po instrukciji): manje je bolje. Ista funkcija s istim instrukcijama i većim CPI-jem znači da
  procesor na nešto **čeka**.
- **PTI**: događaja na 1000 izvršenih instrukcija.
- **Punjenje L1 ("demand refill")**: instrukcija je tražila podatak kojeg nema u L1, pa je linija keša donesena
  iz L2, iz L3 ili iz RAM-a. Broje se samo punjenja na koja je instrukcija čekala. Linije koje je **prefetcher**
  (dio procesora koji prepozna pravilan hod kroz memoriju i unaprijed donosi sljedeće linije) donio na vrijeme
  ovdje se ne vide.
- Apsolutni brojevi uzoraka nisu preračunati u cikluse (19 događaja dijeli 6 brojača); koristimo samo omjere.

Vrijednosti su iz sesija jedne faze (`jacobi`, `advect`, `forces`), 64³ / 128³:

| Funkcija | CPI | Grananja (PTI) | Pogrešno predviđena | Punjenja L1 iz L2 (PTI) | iz L3 (PTI) | iz RAM-a (PTI) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `jacobiSlabAvx2` | 0.54 / 1.45 | 85.8 / 84.8 | 0.18% / 0.12% | 39.3 / 36.2 | 1.47 / 0.84 | 0.00 / 1.50 |
| `setBoundary` (u sesiji `jacobi`) | 0.51 / 2.17 | 108 / 105 | 0.15% / 0.59% | 23.2 / 20.5 | 133 / 42.9 | 0.00 / 52.7 |
| `advectRow3` | 0.38 / 0.38 | 2.8 / 2.3 | 1.25% / 1.18% | 2.8 / 6.1 | 0.11 / 0.04 | 0.03 / 0.05 |
| `advectRow2` | 0.44 / 0.40 | 3.9 / 3.1 | 1.01% / 0.89% | 6.0 / 7.7 | 0.07 / 0.05 | 0.08 / 0.04 |
| `vorticityRow` | 0.67 / 1.62 | 38.1 / 34.4 | 0.22% / 0.20% | 91.6 / 26.0 | 1.32 / 0.64 | 0.79 / 1.13 |
| `confinementRow` | 0.47 / 1.08 | 29.1 / 25.7 | 0.20% / 0.23% | 4.2 / 4.9 | 0.16 / 0.13 | 0.01 / 0.36 |
| `applyBuoyancy` | 0.48 / 0.70 | 86.8 / 82.6 | 0.09% / 0.07% | 1.8 / 4.3 | 0.07 / 0.16 | 0.26 / 0.67 |

Šta se iz ovoga vidi:

- **Jacobi.** Na 128³ izvršava iste instrukcije kao na 64³ (isti broj grananja i pristupa memoriji na 1000
  instrukcija), ali mu za svaku treba 2.7 puta više ciklusa (CPI 0.54 prema 1.45). Grananja ne griješe ni tu ni
  tamo. Jedini brojač koji se mijenja je punjenje iz RAM-a: 0.00 prema 1.50 PTI. Taj broj je mali jer prefetcher
  donosi većinu linija, ali ih na 128³ ne donosi dovoljno brzo: jezgro čeka memoriju.
- **`setBoundary`.** Ovo je najjasniji slučaj. Petlja po plohama `x = 0` i `x = nx+1` u svakom koraku skače na
  drugi red polja, pa prefetcher ne pomaže. Na 64³ gotovo svaki takav pristup puni L1 iz L3 (133 PTI). Na 128³
  polje ne staje u L3, pa punjenja dolaze iz RAM-a (52.7 PTI), a CPI raste s 0.51 na 2.17.
- **Advekcija.** CPI je isti na obje mreže (0.38 do 0.44) i gotovo nema punjenja izvan L2. Advekcija nije
  ograničena memorijom; ograničava je količina računa.
- **Sile.** `vorticityRow` i `confinementRow` na 128³ imaju 2.3 do 2.4 puta veći CPI, slično Jacobiju.
- **Miješanje SSE i AVX koda** (`SSE_AVX_STALLS`) je 0 u svim funkcijama solvera.

**Red koji izgleda pogrešno.** U sesijama `forces` jedan red iz `ucrtbase.dll` ima CPI u hiljadama. To je
`memcpy` kojim `smoke_prof` vraća brzinu prije svakog poziva: jedna instrukcija `rep movsb` kopira cijelo polje,
pa na mnogo ciklusa dolazi vrlo malo izvršenih instrukcija i omjeri gube smisao. Taj red nije dio solvera.

### 4.6 Novi nalaz: čitanja i upisi preko granice linije keša

Kolona `MISALIGNED_LOADS` broji čitanja koja prelaze granicu linije keša (64 B) ili stranice. Na 1000 instrukcija
(64³ / 128³): `jacobiSlabAvx2` 342 / 413, `vorticityRow` 264 / 240, `confinementRow` 158 / 151, `applyBuoyancy`
95 / 170, advekcija 7 do 27.

**Odakle dolazi.** `Field3D` poravnava početak svakog reda na 32 B (red je zaokružen na 8 floatova, cijeli niz
na 64 B). Ali prva unutrašnja ćelija je `i = 1`, dakle 4 B iza početka reda (ćelija `i = 0` je ghost). AVX2
petlja zato čita i piše blokove od 32 B koji počinju na pomaku 4 B: svaki drugi takav blok prelazi granicu
linije od 64 B. U Jacobiju to vrijedi za četiri susjeda (`j±1`, `k±1`), divergenciju i upis; `i+1` je na pomaku
8 B, a jedino je `i−1` poravnat. Po kodu se očekuje oko 3 takva čitanja po prolazu petlje od 12 instrukcija,
dakle oko 250 PTI; izmjereno je 342 do 413. Red veličine se slaže, razlika nije objašnjena.

**Šta nije poznato.** Koliko ciklusa to košta **nije izmjereno**. Tehnika T11 (poravnanje) u PERF_LOG-u je
zabilježena kao "raspored postoji, učinak nije mjeren". Ovo je prvi dokaz da poravnanje reda ne znači poravnanje
onoga što petlja stvarno čita.

Kolona `STLI_OTHER` (čitanje koje mora čekati raniji upis na adresu koja se djelimično preklapa) je povišena u
`vorticityRow` (31 / 121 PTI) i `subtractPressureGradient` (117 PTI na 128³). Uzrok u kodu nije istražen.

### 4.7 Frontend (`inst_access`)

**Šta je frontend.** Procesor radi u dva dijela. *Frontend* donosi instrukcije iz memorije i dekodira ih u mikro
operacije (µop); *backend* ih izvršava. Dekodirane µop-ove procesor čuva u posebnom kešu ("op cache"), pa vruću
petlju ne mora ponovo dekodirati. Ako frontend ne stiže isporučiti µop-ove (promašaj keša instrukcija, promašaj
op cachea, promašaj ITLB-a, tj. keša prevođenja adresa stranica s kodom), backend stoji bez posla.

Konfiguracija `inst_access`, sesije jedne faze, 64³ / 128³:

| Funkcija | Promašaji op cachea (udio pristupa) | Promašaji L1 keša instrukcija (PTI) | Promašaji ITLB-a (PTI) |
| :--- | ---: | ---: | ---: |
| `jacobiSlabAvx2` | 1.3% / 1.4% | 0.42 / 0.48 | 0.00 / 0.00 |
| `setBoundary` (u sesiji `jacobi`) | 1.9% / 1.0% | 1.16 / 0.60 | 0.00 / 0.00 |
| `advectRow3` | 4.1% / 3.6% | 0.60 / 0.60 | 0.00 / 0.00 |
| `advectRow2` | 12.6% / 5.6% | 0.58 / 0.51 | 0.00 / 0.00 |
| `vorticityRow` | 1.7% / 2.4% | 0.45 / 0.57 | 0.00 / 0.01 |
| `confinementRow` | 1.3% / 1.9% | 0.29 / 0.40 | 0.00 / 0.00 |

- U Jacobiju, silama i `setBoundary` frontend gotovo nikad ne promašuje: vruće petlje su kratke i ostaju u op cacheu.
- Advekcija ima veće petlje (jedan prolaz obrađuje do tri polja), pa op cache promašuje češće, naročito
  `advectRow2` (5.6 do 12.6%). Njen CPI je ipak najniži u solveru (0.38 do 0.44), pa to nije vidljivo usko grlo.
- CPI iz ovih sesija se slaže s onim iz `assess_ext` (Jacobi 0.53 / 1.37 prema 0.54 / 1.45), iako su to zasebna
  pokretanja s drugim skupom brojača.
- **Kolona koju ne koristimo.** `L2_CACHE_MISSES_FROM_IC_MISSES` daje 5 do 27 PTI za Jacobi i `setBoundary`,
  više nego što uopšte ima promašaja keša instrukcija (ispod 1.2 PTI), i raste tamo gdje rastu promašaji
  *podataka*. Te dvije kolone se ne slažu, pa iz nje ne izvodimo ništa. Uzrok nije utvrđen.
- **Granica mjerenja.** Ovi brojači pokrivaju keš instrukcija, op cache i ITLB. Brojača za zastoje pri
  otpremanju µop-ova u ovim konfiguracijama nema, pa "frontend zastoja nema" nije dokazano u punom smislu.

---

## 5. Kandidati za optimizaciju, poredani po dokazu

Ništa od ovoga **nije implementirano**. Odluka je na tački G0, a svaka izmjena ide po protokolu iz
OPTIMIZATION.md (hipoteza, tačnost, mjerenje po D10, unos u PERF_LOG.md).

| # | Kandidat | Dokaz | Očekivani domet | Mijenja rezultat? |
| ---: | :--- | :--- | :--- | :--- |
| 1 | `setBoundary`: bez množenja za skalarna polja, petlje po plohama koje se vektoriziraju (nivo 1: T10, T12) | 7.5 do 8.5% cijelog koraka, skalarni `mulss` (4.4). Brojači: plohe `x` pune L1 iz L3 (64³) odnosno iz RAM-a (128³), CPI na 128³ je 2.17 (4.5) | najviše taj udio; koliko stvarno, nije izmjereno. Dio troška je čekanje na memoriju, a njega uklanjanje množenja ne dira | ne mora (kopija umjesto množenja s 1 je bit po bit ista) |
| 2 | Poravnanje onoga što petlje stvarno čitaju (nivo 1: T11): pomjeriti raspored tako da `i = 1` padne na granicu od 32 B | 342 do 413 čitanja preko granice linije na 1000 instrukcija u Jacobiju, 150 do 260 u silama (4.6) | **nije izmjereno**; trošak jednog takvog čitanja na Zen 3 ne znamo. Mijenja `Field3D`, pa dira sve faze i `DensityView` | ne (isti račun, drugi raspored u memoriji) |
| 3 | Jacobi na 128³: manje prometa memorije (npr. upis bez "write allocate", nivo 2: fino podešavanje memorije) | E ≈ 0.94, 2.03x po ćeliji, CPI 2.7x veći nego na 64³ (H1) | nije izmjereno; na mrežama koje staju u keš može biti sporije, pa bi išlo samo za velike mreže | ne |
| 4 | Jacobi: množenje s 1/6 umjesto `vdivps` | H2 nije razdvojena; eksperiment bi je razdvojio | nije izmjereno | **da** (drugačije zaokruživanje; D11 to isključuje dok tim ne odluči) |
| 5 | Advekcija: pravi `vgatherdps` ili manje pritiska na registre | oko 36% uzoraka advekcije u slaganju vektora i pojedinačnim čitanjima (H3); CPI 0.38 do 0.44, dakle procesor ne čeka, nego ima mnogo instrukcija | nije izmjereno; M4-014 bilježi da ručni gather na Zen 3 nije provjeren | ne |
| 6 | Sile na 128³ | 1.37x po ćeliji prema 64³ (uključuje `memcpy` iz `smoke_prof`); CPI `vorticityRow` i `confinementRow` 2.3 do 2.4x veći nego na 64³ | srednji dokaz da su na 128³ ograničene memorijom; šta uraditi, nije razrađeno | ne |

**Šta još nedostaje.** H2 nije razdvojena (treba eksperiment ili IBS). Trošak čitanja preko granice linije nije
izmjeren. Nijedan kandidat nema izmjereno ubrzanje: to se mjeri tek kad se izmjena napravi, po protokolu D10.
