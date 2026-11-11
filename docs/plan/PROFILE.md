# PROFILE: hardver i profil serijske verzije (M4d)

Ovaj dokument odgovara na pitanje **gdje procesor troši vrijeme i zašto** (keš promašaji, grananja, zastoji),
a ne koliko je nešto brže. Zaključci o brzini ostaju na S i Δ iz [PERF_LOG.md](PERF_LOG.md) (protokol D10).
Radni plan: [EXECUTION_PLAN.md](EXECUTION_PLAN.md), faze P0 i P1.

Stanje: **odjeljci 1 i 2 su popunjeni (P0, 2026-11-11)**. Odjeljci 3 do 5 se popunjavaju u P1 (AMD uProf).

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

Popunjava se u P1: verzija AMD uProf-a, profilna gradnja (`Solver/build-prof`), program `smoke_prof`,
konfiguracije skupljanja (tbp, assess, IBS) i gdje su sirovi podaci (`perf/uprof/`).

## 4. Hipoteze H1 do H5 i odgovori (P1)

| ID | Hipoteza | Odgovor | Dokaz |
| :--- | :--- | :--- | :--- |
| H1 | Jacobi na 128³ je ograničen memorijom (L2/L3 promašaji) | djelimično: E ≈ 0.94 (odjeljak 2.3); brojači nisu izmjereni | P1 |
| H2 | Jacobi na 64³ je ograničen dijeljenjem (`divps`) | nije izmjereno | P1 |
| H3 | Advekcija troši vrijeme na oponašani gather | nije izmjereno | P1 |
| H4 | Grananja u vrućim jezgrima rijetko griješe | nije izmjereno | P1 |
| H5 | Frontend zastoja nema | nije izmjereno | P1 |

## 5. Kandidati za optimizaciju, poredani po dokazu (P1)

Popunjava se u P1. Optimizacije se **ne implementiraju** prije tačke G0 i bez protokola iz OPTIMIZATION.md.
