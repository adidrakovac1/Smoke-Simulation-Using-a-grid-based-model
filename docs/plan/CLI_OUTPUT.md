# CLI_OUTPUT: šta konzolni program ispisuje

Do zadnjeg milestone-a (M-VIZ, vizualizacija u rendereru) cijeli projekat se pokreće iz
**konzolnog programa `smoke_cli`**, bez prozora. Ovaj dokument određuje šta taj program ispisuje i
kako se pokreće, jer isti izlaz služi za provjeru ispravnosti, za mjerenje i za izvještaj.

Povezano: [VERIFICATION.md](VERIFICATION.md), [OPTIMIZATION.md](OPTIMIZATION.md), [PERF_LOG.md](PERF_LOG.md).

---

## 1. Principi

1. **Konzola prvo.** Program ispisuje tekst i (opcionalno) snima slike presjeka. Dim se može
   "vidjeti" bez renderera, a mjerenja nisu zagađena crtanjem.
2. **Ne samo rečenice, nego brojevi.** Tekst koji kaže "počinjemo" je dobra poruka za korisnika, ali
   zaključci se donose iz brojeva (vrijeme po fazi, dijagnostika, kontrolni zbir).
3. **Ispis nikad ne ulazi u izmjereno vrijeme.** Štampanje u konzolu je sporo. Tajmer mjeri samo
   `step()`; ispis se radi između koraka, i u modu mjerenja samo na kraju.
4. **Isti izlaz za ljude i za skripte.** Čitljiv tekst po zadanom; opciono CSV za evidenciju.
5. **Ispravan izlazni kod.** `0` ako je sve u redu; različito od `0` ako ima `NaN`/`Inf` ili ne prođe
   poređenje s referentnim rezultatom. Tako se greška uoči i bez čitanja ispisa.
6. **Samo ASCII u ispisu** (bez č, ć, š, ž, đ). Windows konzola često ne prikaže ta slova ispravno
   (kodna stranica). Tekst je na bosanskom bez dijakritika, npr. "Pocinjemo simulaciju dima".

Napomena o riječi: u konzoli ne **iscrtavamo** dim, nego ga **simuliramo**. Iscrtavanje dolazi tek u
M-VIZ. Zato poruka kaže "simulaciju", da ne zbunjuje kad se doda renderer.

---

## 2. Raspored ispisa (primjer; vrijednosti u ovom dokumentu su oznake mjesta, ne rezultati)

```
=== Smoke Solver (serijska verzija) ===
Pocinjemo simulaciju dima.

Racunar:    <procesor>, <jezgara/niti>, AVX2: <da/ne>
Gradnja:    <Release>, <kompajler>, oznake: <...>
Konfig:     mreza <nx>x<ny>x<nz>, koraka <N> (+ <W> zagrijavanja), dt <dt>,
            Jacobi iteracija <K>, temperatura <da/ne>, vorticity <eps>
Izvor dima: <opis>

Napredak:
  [  10/ <N>]  <ms> ms/korak   max|div| <prije> -> <poslije>   masa <m>
  [  20/ <N>]  ...
  ...

Zavrseno. Ukupno vrijeme: <s> s (<N> koraka)

Vrijeme po fazi (medijan po koraku):
  Faza                      ms       udio
  Izvor dima                <..>     <..>%
  Sile (uzgon, vorticity)   <..>     <..>%
  Advekcija brzine          <..>     <..>%
  Projekcija (Jacobi)       <..>     <..>%
  Advekcija gustoce         <..>     <..>%
  UKUPNO po koraku          <..>     100%

Propusnost (projekcija):   <GB/s>
Dijagnostika:
  max|div| prije/poslije projekcije:  <..> / <..>
  ukupna masa dima:                    <..>
  max|u|, CFL:                         <..>, <..>
  NaN/Inf celija:                      0
Kontrolni zbir gustoce (za poredjenje verzija): <heks>
Slike presjeka: <folder> (svakih <M> koraka)
Status: OK
```

- **Napredak** se ispisuje svakih nekoliko koraka, ne svaki (mod mjerenja ga isključuje).
- **Vrijeme po fazi** je glavna korist za optimizaciju: odmah se vidi gdje se troši vrijeme (procjena `f` za Amdahlov zakon).
- **Kontrolni zbir** gustoće na kraju omogućava brzo "isti li je rezultat" između verzija. Za verzije koje smiju
  imati male razlike (SIMD, GPU) koristi se poređenje s tolerancijom (VERIFICATION.md, odjeljak 3).

---

## 3. Parametri komandne linije (predlog)

| Parametar | Značenje |
| :--- | :--- |
| `--grid N` ili `--nx/--ny/--nz` | veličina mreže (zadano 64) |
| `--steps N`, `--warmup W` | broj mjerenih koraka i zagrijavanja |
| `--dt X`, `--jacobi K` | vremenski korak, broj Jacobi iteracija |
| `--seed S` | sjeme za determinističke testove |
| `--quiet` | bez napretka; samo sažetak (za mjerenje) |
| `--csv FAJL` | dopisuje liniju u CSV (šema u PERF_LOG.md) |
| `--dump-slices FOLDER [--every M]` | snima presjek gustoće kao `.pgm` slike |
| `--verify FAJL` | poredi s referentnim rezultatom; izlazni kod `!= 0` ako ne prolazi |
| `--calibrate` | ispisuje otisak računara i kalibracione vrijednosti (OPTIMIZATION.md, odjeljak 5.3) |

---

## 4. Odluka D7 (dodati u PLAN.md, odjeljak 3)

| ID | Pitanje | Preporuka |
| :--- | :--- | :--- |
| D7 | Jezik i kodiranje ispisa konzolnog programa | Bosanski bez dijakritika (ASCII); isti raspored za sve članove |
