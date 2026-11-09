# VERIFICATION: kako znamo da radi ispravno i kako mjerimo

Dva odvojena pitanja: **je li tačno** (testovi, odjeljci 1 do 3) i **koliko je brzo** (mjerenje, odjeljak 4).
Prvo tačnost, tek onda brzina: brza pogrešna simulacija ne vrijedi ništa.

---

## 1. Testovi tačnosti (jedinični i integracijski)

Testovi su u `Solver/tests/`, pokreću se jednom komandom (`smoke_tests`) i ne trebaju prozor.
Svaki test ima fiksne ulaze, pa je rezultat ponovljiv. Alat: doctest.
Isti testovi se pokreću i komandom `smoke_cli --selftest` (za računare na kojima Windows blokira `smoke_tests.exe`); izlazni kod je `0` ako svi prođu, a `1` ako ijedan padne.

| ID | Šta provjerava | Kako | Očekivanje |
| :--- | :--- | :--- | :--- |
| T1 | Advekcija pomjera dim | kugla gustoće u polju konstantne brzine `u = (c,0,0)`, jedan korak | težište se pomjeri za `c·dt/h` ćelija, u okviru greške interpolacije |
| T2 | Nulta brzina ne mijenja gustoću | `u = 0`, više koraka advekcije | gustoća nepromijenjena (bit po bit) |
| T3 | Projekcija smanjuje divergenciju | slučajno (fiksno `seed`) polje brzine, projekcija s K iteracija | `max|div|` poslije / prije je ispod praga; opada s većim K |
| T4 | Ograničenost advekcije | proizvoljna gustoća, jedan korak | svaka nova vrijednost leži u `[min, max]` starih vrijednosti okoline (svojstvo trilinearne interpolacije) |
| T5 | Rubni uslovi | poslije koraka | komponenta brzine okomita na zid je 0; gustoća i pritisak jednaki susjednoj unutrašnjoj ćeliji |
| T6 | Simetrija | simetričan početni uslov (npr. izvor u sredini po x i z) | izlaz je simetričan (hvata greške u indeksiranju) |
| T7 | Determinizam | dva pokretanja istog scenarija | identični rezultati bit po bit |
| T8 | Nema NaN/Inf | 500 koraka s tipičnim parametrima | sve vrijednosti konačne |
| T9 | Promjena veličine | `resize` 64³, pa 32³, pa 128³ | nema pada programa, polja su nulirana, dimenzije tačne |
| T10 | Sačuvana ukupna masa (približno) | zatvorena kutija bez izvora | ukupna masa se **ne mora** tačno održati (semi-Lagrangian ne čuva masu tačno), ali promjena je ograničena; **prag određujemo mjerenjem, ne pogađamo** |

**O pragovima:** brojevi u T3 i T10 mjere se na stvarnom solveru, pa se upisuju u test kao komentar
s datumom i veličinom mreže.

**Napomena o T1.** Daleko od zidova linearna interpolacija s konstantnom brzinom pomjera težište **tačno**
za `c·dt` (dokaz u IMPLEMENTATION_PLAN.md, odjeljak 4.4). Zato je tolerancija za T1 reda greške zaokruživanja
`float`, a ne "greške interpolacije". Tačan broj upisujemo poslije mjerenja (M1), a kugla u testu je daleko
od zidova.

**Napomena o T6.** Simetrija se ne može provjeriti bit po bit: zrcalna koordinata `(n+1-i) + dt·u` se
drugačije zaokružuje nego `i - dt·u`. T6 zato poredi s tolerancijom, koju određujemo mjerenjem (M1).

**T3, izmjereno (2026-11-09, g++ 16.2.0, Release, sjeme 42).** Slučajno polje u `[-1, 1)`: omjer
`max|div|` poslije / prije za K = 10, 20, 40, 80 je 0.721, 0.715, 0.711, 0.710 na 32³ i 0.745, 0.740, 0.737,
0.736 na 64³; granica je **0.8**, uz strogo opadanje s K. Omjer ostaje visok jer bijeli šum ima mnogo
"šahovske" komponente koju projekcija na mreži s vrijednostima u centru ćelije ne može ukloniti
(MATH.md, odjeljak 5). Zato postoji i dopunski test na glatkom polju (gradijent Gausove funkcije, 16³): 0.421,
0.117, 0.111, 0.111 za K = 10, 100, 1000, 5000, granica **0.15** za K = 1000. Namjerno pogrešan predznak u
Jacobiju daje omjere od 1.4 do 1.9 koji rastu s K, pa oba testa padaju.

**Napomena o T3.** Divergencija poslije projekcije ne ide u nulu (objašnjenje u MATH.md,
odjeljak 5). Zato test traži omjer prije/poslije i monotono opadanje po iteracijama, a ne `== 0`.

**T10, izmjereno (2026-11-09, Apple M4, AppleClang 21.0.0, Release).** Na zatvorenoj 24³ mreži,
bez izvora, gusta kugla je advektirana 100 puta poljem kružnog toka. Relativna promjena mase bila je
**7.29746%**; prag testa je **10%**. To je tolerancija za ovu postavku semi-Lagrangian advekcije,
ne tvrdnja da algoritam čuva masu.

---

## 2. Dijagnostika u toku rada

`SmokeSolver::diagnostics()` vraća, a konzolni program ispisuje svakih N koraka:
- `max|∇·u|` prije i poslije projekcije,
- ukupna masa dima (`Σρ`),
- `max|u|` i CFL broj `max|u|·dt/h`,
- broj ćelija s `NaN`/`Inf` (mora biti 0).

Ovo je prva linija odbrane: ako nešto krene krivo, vidi se u brojevima prije nego u slici.

---

## 3. Referentni rezultat za poređenje paralelnih verzija

Serijska referenca je snimljena:
1. `smoke_bench --reference Solver/reference/serial_64_100steps.bin --steps 100` pokreće fiksni
   scenarij na 64³ bez zagrijavanja i snima `density` kao little-endian IEEE-754 float32 u
   z/y/x redoslijedu; sažetak i parametri su u pratećem `.bin.txt` fajlu.
2. Referentni fajl je `Solver/reference/serial_64_100steps.bin`; FNV-1a 64-bit kontrolni zbroj je
   `165cde9630821391`, min 0, max 12.9589844, sredina 0.032197039. B0 i B1 daju identičan rezultat
   (najveća apsolutna razlika 0).
3. Ista komanda podržava druge veličine (`--grid N`) kad se želi dodatna referenca.
4. Svaka kasnija verzija (OpenMP, SIMD, GPU) se poredi s ovim fajlom:

| Verzija | Očekivano poređenje | Napomena |
| :--- | :--- | :--- |
| OpenMP (isti redoslijed po ćeliji) | identično ili vrlo blizu | Jacobi i advekcija nemaju ovisnost među ćelijama; razlike mogu doći samo od redukcija (suma) |
| SIMD | mala relativna razlika | može nastati zbog drugačijeg redoslijeda operacija ili fuzije množenja i sabiranja; tolerancija se određuje mjerenjem |
| GPU | veća tolerancija | drugačija aritmetika i redoslijed |

(Ovo su očekivanja, a ne garancije. Stvarne tolerancije upisujemo nakon prvog mjerenja.)

Ovo je ekvivalent `verify()` iz Lab1 koji spominje planski dokument.

---

## 4. Mjerenje performansi

Profilisanje dolazi prije paralelizacije jer bez njega ne znamo šta paralelizirati.

> Za poređenje varijanti (osnova naspram optimizacije), normalizaciju po računaru (ubrzanje S,
> delta Δ, efikasnost memorije E) i vođenje evidencije vrijedi **OPTIMIZATION.md** (odjeljci 4 do 6)
> i **PERF_LOG.md**. Ovaj odjeljak opisuje samo osnovno mjerenje serijske verzije.

### 4.1 Šta mjerimo
- Vrijeme **svake faze koraka** (izvor, sile, advekcija brzine, projekcija, advekcija gustoće)
  i ukupno, u milisekundama. Realizuje se tajmerom (`ScopedTimer`) oko poziva faze, na
  `std::chrono::steady_clock`.
- Za projekciju posebno: vrijeme po Jacobi iteraciji.

### 4.2 Kako mjerimo (da brojevi vrijede)
- **Release gradnja** s optimizacijama zadanog kompajlera, nikad Debug. Debug je desetke puta sporiji
  i bezvrijedan za ubrzanje; konkretne oznake se upisuju uz rezultat.
- Prije mjerenja **10 koraka zagrijavanja**, zatim **100 koraka** koji se broje. Prijavljuje se
  **medijan** i raspon, ne jedno mjerenje.
- Fiksni scenarij, parametri i `seed`. Svako mjerenje na veličinama 32³, 64³, 96³, 128³.
- Isti računar, priključen na struju (laptop u štednji energije daje besmislene brojeve),
  ugašeni drugi teški programi.
- Zapiše se opis mašine uz rezultate: procesor, broj jezgara, RAM, kompajler i oznake.

### 4.3 Izlaz
`smoke_bench` po zadanom mjeri 32³, 64³, 96³ i 128³, uz 10 koraka zagrijavanja i 100 mjerenih
koraka; `--grid N`, `--warmup N`, `--steps N` i `--output FILE` mijenjaju postavke. CSV ima
`grid,stage,median_ms,min_ms,max_ms,steps`, sa redovima za pet faza, `jacobi_iteration` (prosjek
po iteraciji, unutar projekcije) i `total`. `smoke_bench --calibrate` ispisuje otisak mašine,
memorijski triad i latenciju zavisnog lanca.

**M3, izmjereno 2026-11-09:** B1 medijani u ms po koraku (100 koraka + 10 zagrijavanja), Apple M4,
AppleClang 21.0.0.21000101, Release, `-O3 -DNDEBUG`, `dt=1/60`, K=40, `ε=5`, jedna nit:

| Mreža | Izvor | Sile | Advekcija brzine | Projekcija | Advekcija skalara | Ukupno | Udio projekcije `f` |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32³ | 0.0061 | 0.2151 | 0.5589 | 0.8349 | 0.3717 | 1.9930 | 41.89% |
| 64³ | 0.0291 | 1.6371 | 4.5709 | 5.6225 | 3.0498 | 15.0142 | 37.45% |
| 96³ | 0.0921 | 5.2146 | 15.4112 | 19.4295 | 10.2533 | 50.3578 | 38.58% |
| 128³ | 0.2578 | 11.9998 | 36.2063 | 47.5469 | 24.2548 | 120.4847 | 39.46% |

Udio je medijan faze podijeljen medijanom ukupnog koraka; medijani pojedinačnih faza ne moraju se
tačno sabrati u medijan ukupnog vremena. Projekcija je najveća faza na sve četiri mreže; to je
izmjeren nalaz, ne pretpostavka. Svi B0/B1 redovi, rasponi i ubrzanja su u `perf/results.csv`.

### 4.4 Metrike za izvještaj
- **Udio svake faze** u ukupnom vremenu. To je procjena `f` za Amdahlov zakon.
- **Ubrzanje** `= vrijeme serijske / vrijeme paralelne` za 1, 2, 4, ... niti (kasnije).
- **Propusnost memorije** za Jacobi: broj ćelija × broj iteracija × (broj pristupa po ćeliji ×
  4 bajta) / vrijeme. Jedan Jacobi prolaz čita oko 7 vrijednosti i upisuje 1, bez računanja
  keša; stvarni promet je manji zbog keša. Uz GFLOP/s dobijamo sliku je li program ograničen
  računanjem ili memorijom.

---

## 5. Vizuelna provjera (dopuna, ne zamjena)

Brojevi hvataju greške koje oko ne vidi, a oko hvata ono što testovi zaborave.
- Konzolni program može upisati presjek gustoće (npr. sredina po y) kao sliku (`.pgm`) svakih N
  koraka, pa se dim vidi i bez renderera.
- U rendereru (zadnji milestone, M-VIZ) se dim gleda uživo.

Tipični simptomi: dim se širi samo u jednu stranu (greška znaka ili indeksa), "eksplozija"
(pogrešan dt ili formula), dim ne izlazi iz izvora (uzgon bez efekta), kvadratni artefakti
(rubni uslovi).
