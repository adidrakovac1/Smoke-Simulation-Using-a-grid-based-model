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

**O pragovima:** brojeve u T3 i T10 ne smijemo izmisliti unaprijed. U fazi 2 se izmjere na
stvarnom solveru, pa se upišu u test kao komentar s datumom i veličinom mreže.

**Napomena o T3.** Divergencija poslije projekcije ne ide u nulu (objašnjenje u MATH.md,
odjeljak 5). Zato test traži omjer prije/poslije i monotono opadanje po iteracijama, a ne `== 0`.

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

Kad je serijska verzija gotova i provjerena:
1. Pokrene se fiksan scenarij (veličina mreže, parametri, `seed`, 100 koraka) i snimi se
   `density` na kraju kao binarni fajl `Solver/reference/serial_64_100steps.bin` + sažetak (kontrolni zbroj,
   min, max, srednja vrijednost).
2. Isto za 32³ i 128³ ako je izvodljivo.
3. Svaka kasnija verzija (OpenMP, SIMD, GPU) se poredi s ovim fajlom:

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
- **Release gradnja** (`-O2`), nikad Debug. Debug je desetke puta sporiji i bezvrijedan za ubrzanje.
- Prije mjerenja **10 koraka zagrijavanja**, zatim **100 koraka** koji se broje. Prijavljuje se
  **medijan** i raspon, ne jedno mjerenje.
- Fiksni scenarij, parametri i `seed`. Svako mjerenje na veličinama 32³, 64³, 96³, 128³.
- Isti računar, priključen na struju (laptop u štednji energije daje besmislene brojeve),
  ugašeni drugi teški programi.
- Zapiše se opis mašine uz rezultate: procesor, broj jezgara, RAM, kompajler i oznake.

### 4.3 Izlaz
`smoke_bench` piše CSV (jedna linija po veličini mreže i fazi), npr.:
`grid,stage,median_ms,min_ms,max_ms,steps`. Iz CSV-a se prave tabele i grafici za izvještaj.

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
