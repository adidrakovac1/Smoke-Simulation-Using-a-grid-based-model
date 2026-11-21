# MATH: šta simulacija računa i zašto

Ovaj dokument objašnjava matematiku jednog koraka simulacije, redom kojim se izvršava u
kodu. Služi da se razumije šta se piše, ne samo kako. Formule slijede literaturu iz `docs/`.
Gdje se pozivamo na rad, navedeno je koji je; **konstante i tačne diskretizacije tek
provjeravamo u izvornim člancima prilikom implementacije**, i nijedna vrijednost ovdje
nije izmišljena.

Literatura (sve u `docs/`):
- **[Stam99]** Stable Fluids (`stable_fluids.pdf`): semi-Lagrangian advekcija i projekcija.
- **[Stam03]** Real-Time Fluid Dynamics for Games (`Real-Time Fluid Dynamics for Games.pdf`): kratki C kod, 2D, `set_bnd`, `project`, `advect`.
- **[Fedkiw01]** Visual Simulation of Smoke (`Visual Simulation of Smoke.pdf`): uzgon, temperatura, vorticity confinement, 3D.
- **[Harris04]**, **[Crane07]**: GPU Gems (poglavlja 38 i 30), za kasniju GPU fazu i za crtanje volumena.
- **[Amdahl67]**: za analizu ubrzanja.

---

## 1. Mreža i oznake

- Prostor je kocka od `nx × ny × nz` ćelija. Ćelija `(i, j, k)` čuva sve veličine u svom centru.
- `h` je dužina stranice ćelije. **Odluka D4 (donesena): radimo u jedinicama ćelije, dakle
  `h = 1`.** Brzina je tada "ćelija u sekundi", pa nema faktora `h` u formulama.
  Cijena: parametri (uzgon i sl.) se ne prenose automatski s 64³ na 128³ i podešavaju se
  ponovo. Alternativa je fiksna fizička veličina kocke, uz `h = 1/N`.
- Veličine po ćeliji: gustoća `ρ`, temperatura `T`, brzina `u = (u, v, w)`, pritisak `p`.
- Susjedi: `p(i±1)`, `p(j±1)`, `p(k±1)`. Sve se računa iz najbližih susjeda, i to je razlog zašto
  se simulacija dobro paralelizira.

## 2. Jedan korak: redoslijed

1. Dodaj izvor dima (i topline).
2. Sile: uzgon (+ opcionalno vorticity confinement).
3. Advekcija brzine.
4. Projekcija (popravka da zrak ne nestaje i ne gomila se).
5. Advekcija gustoće (i temperature) popravljenom brzinom.

Isti redoslijed imaju [Stam99] i [Fedkiw01]. Projekcija je dio koji troši najviše vremena.
[Stam03] (`vel_step`) projicira dva puta, prije i poslije advekcije brzine; mi projiciramo jednom, kao
[Fedkiw01]. Druga projekcija je moguća kasnija opcija za kvalitet.

---

## 3. Izvor i sile

### Izvor
U nekoliko ćelija pri dnu povećavamo `ρ` (i `T`). Oblik izvora (kugla, disk), radijus i jačina
su parametri. Za testove je izvor fiksan i determinističan.

### Uzgon (buoyancy)
Topao i rijedak zrak se diže, gust dim teži dolje. [Fedkiw01] to opisuje jednostavnim modelom,
jednadžbom (8) u radu: sila prema gore proporcionalna temperaturi (iznad ambijentalne) i sila
prema dolje proporcionalna gustoći dima, s dvije pozitivne konstante (u radu `α` i `β`).
Ambijentalna temperatura je `T_amb`:

```
f_uzgon = ( -α · ρ  +  β · (T - T_amb) ) · (jedinični vektor "gore")
u  ←  u + dt · f_uzgon
```

**Odluka D1 (donesena: uvodimo `T`):** naš plan navodi samo gustoću, brzinu i pritisak, a rečenica "topao i rijedak zrak
se diže" traži temperaturu. Preporuka: uvesti polje `T` (jedno polje više) jer daje
vjerodostojnije kovitlanje, uz mogućnost da se `β = 0` pa se model svede na gustoću.
Vrijednosti `α`, `β` biramo eksperimentalno, ne prepisujemo ih iz rada.

**Smjer "gore":** [Fedkiw01] uzima `z`, a mi `+y` (ARCHITECTURE.md, odjeljak 8). Zato uzgon mijenja
komponentu `v`: `v ← v + dt · ( -α·ρ + β·(T - T_amb) )`.

**Izvor (odobreno 2026-11-09):** kao `add_source` u [Stam03], izvor **dodaje** `dt · brzina`:
`ρ += dt · densityRate`, `T += dt · temperatureRate`, u ćelijama unutar kugle izvora. Pozicija i poluprečnik
su relativni (poluprečnik kao udio od `nx`), da ne zavise od veličine mreže.

**Izabrane vrijednosti (M2, 2026-11-09, eksperimentalno na 64³, gledanjem presjeka):**

| Parametar | Vrijednost | Zašto ova |
| :--- | :--- | :--- |
| izvor: pozicija | `(0.5, 0.1, 0.5)` | dno, sredina po x i z |
| izvor: poluprečnik | `0.08·nx` (oko 5 ćelija) | sa `0.06` stub je bio preuzak |
| `densityRate`, `temperatureRate` | `10`, `10` po sekundi | prvi pokušaj; dim je vidljiv i ne zasiti sliku |
| `α` | `0.05` | mali; težina dima samo blago koči uspon |
| `β` | `2.0` | sa `β = 1` stub je poslije 300 koraka (5 s) jedva prešao pola kutije; sa `2.0` udari u vrh oko koraka 300 |
| `T_amb` | `0` | temperatura je mjerena od ambijenta |
| vorticity confinement `ε` | `5.0` | odabran vizualno na 64³; `0.5` je jedva mijenjao presjek, `5` vraća vidljive kovitlace bez NaN-a |

Rezultat bez confinementa (64³, 500 koraka, K = 40): stub se diže, udari u zatvoreni vrh (D3) i
razlije se ispod njega; `max|u|` je oko 47 ćelija/s, a CFL oko 0.78. Vrijednosti se ne prenose
automatski na druge veličine mreže (odluka D4).

### Vorticity confinement (opcionalno)
Numerička difuzija izgladi sitne vrtloge i dim izgleda "mrtvo". [Fedkiw01] (jednadžbe 9 do 11)
vraća ih silom:

```
ω   = ∇ × u                  (vorticity, "koliko se zrak vrti")
N   = ∇|ω| / |∇|ω||           (smjer prema jačoj vrtložnosti)
f_c = ε · h · (N × ω)         (ε je parametar; ε = 0 isključuje)
```

U kodu se `ω` i `∇|ω|` računaju centralnim razlikama (`h = 1`), a `|ω|` koristi skalarne rubne
uslove. Ako je `|∇|ω|| = 0`, ćelija ne prima confinement silu. Sila se dodaje kao `u += dt·f_c`
nakon uzgona i prije advekcije brzine; `ε <= 0` je brza putanja bez confinementa.

**M3 podešavanje (2026-11-09, 64³, K = 40, 300 koraka):** upoređeni su `ε = 0, 0.5, 2, 5, 10`
presjeci po y. `ε = 5` daje jasno vidljivo uvijanje, dok je `0.5` jedva mijenjao presjek; `10`
je previše rasuo stub. Za `ε = 5`, CFL je 0.699, `max|u|` 41.95 ćelija/s i nije bilo NaN/Inf.
Zadana vrijednost je zato `5.0`; postaviti `0` za isključivanje. Ovo je vizualno podešena
vrijednost za 64³, a ne univerzalna fizička konstanta. Sa zadanim `ε = 5`, 500 koraka na 64³
završilo je bez NaN/Inf, s CFL 0.644 i `max|u|` 38.63 ćelija/s.

**Nalaz iz pregleda M3 (2026-11-09, g++ 16.2.0, Release, 64³, 100 koraka): confinement povećava
divergenciju koja ostaje poslije projekcije.** `max|div|` prije / poslije projekcije:

| ε | K | prije | poslije | rms poslije |
| :--- | :--- | ---: | ---: | ---: |
| 0 | 40 | 0.583 | 0.154 | 0.0063 |
| 1 | 40 | 1.25 | 0.419 | |
| 2 | 40 | 2.15 | 0.876 | |
| 5 | 40 | 4.58 | 1.94 | 0.0319 |
| 5 | 1000 (zadnji korak) | 4.58 | 1.87 | 0.0313 |
| 5 | 5000 (zadnji korak) | 4.58 | 1.87 | 0.0313 |
| 0 | 5000 (zadnji korak) | 0.583 | 0.106 | 0.0024 |

- Najveća preostala divergencija **nije uz zidove**, nego u unutrašnjosti, odmah iznad izvora
  (ćelija (29,12,29) na 64³), gdje uzgon stvara oštre vrtloge pa je i confinement najjači. Uz
  zidove (prve tri ćelije) je ispod 0.24.
- Uz K = 5000 Poissonova jednačina je riješena (`max|∇²p − div|` = 0.0002), a divergencija i
  dalje ostaje 1.87. Dakle uzrok **nije** spora konvergencija Jacobija, niti greška u confinementu
  (implementacija odgovara [Fedkiw01], jed. 9 do 11). To je granica projekcije na kolociranoj
  mreži (odjeljak 5, "Važna napomena o tačnosti"): kratkotalasne komponente brzine, koje
  confinement pojačava, projekcija ne vidi. Isti "pod" postoji i bez confinementa (0.106).
- Posljedica: ε je kompromis između vidljivog kovitlanja i divergencije. Mjerenje je urađeno
  privremenim programom koji ponavlja `SmokeSolver::step` preko funkcija faza (nije u repozitoriju).
  Izbor ε je odluka tima; referenca i osnove B0/B1 se snimaju tek kad je ε konačan.

### Više izvora (odluka D22, 2026-11-21)
Uz glavni izvor (`SmokeParams::source`) može biti još do 7 izvora (`extraSources`, `extraSourceCount`).
Svaki je ista kugla kao glavni i dodaje `dt · rate` u svoje ćelije; gdje se kugle preklapaju, doprinosi se
saberu. Zadano nema dodatnih izvora, pa je rezultat bit po bit isti kao prije (`smoke_bench --verify`).

### Ventilator: vanjska sila (odluka D22, 2026-11-21)
Navier-Stokesova jednačina ima član vanjske sile `f`; [Stam03] ga dodaje kao `u ← u + dt · f` (`add_source`
na brzini), [Fedkiw01] isto (odjeljak 4). Uzgon je jedna takva sila; ventilator je druga. Oblik polja sile
ventilatora rad ne daje, pa je ovo **naš model**: cilindar ispred ventilatora, centar lica `c`, jedinična osa
`n`, poluprečnik `R`, domet `L`, ciljna brzina `V` (ćelija/s) i odziv `k` (1/s). Za centar ćelije `p`:

```
a  = (p − c) · n                (udaljenost duž ose)
r² = |p − c|² − a²              (kvadrat udaljenosti od ose)
ako je 0 ≤ a ≤ L i r² ≤ R²:   cilj = V · (1 − a / L),   un = u · n
                              ako je un < cilj:   u ← u + min(1, dt · k) · (cilj − un) · n
```

- To je i dalje `u ← u + dt · f`, sa silom `f = k · (cilj − un) · n`: pravi ventilator tjera zrak na određenu
  brzinu (kućni ventilator nekoliko m/s), a ne ubrzava ga bez granice, i ne koči zrak koji je već brži.
- **Prva verzija** (2026-11-21, isti dan) je bila stalno ubrzanje `u ← u + dt · s · (1 − a/L) · n` sa
  `s = 2 m/s²`. U kući je uzgon bio jači, pa se dim skoro samo penjao; Ermin je tražio da dim "stvarno ide u
  pravcu u kojem ga ventilator puše". S ciljnom brzinom (3 m/s, odziv 4/s) dim vidljivo ide duž ose i kroz
  otvor u susjednu sobu (snimak odozgo poslije 10 s).
- Ciljna brzina slabi linearno od lica do kraja dometa, pa na kraju cilindra nema skoka. Iza ventilatora
  (`a < 0`) nema sile.
- Djeluje u koraku 2 (sile), odmah poslije uzgona; projekcija u koraku 4 zatim pravi polje bez divergencije,
  pa zrak koji ventilator gura obilazi namještaj i vraća se kroz ostatak kuće.
- Do 8 ventilatora (`SmokeParams::fans`, `fanCount`); zadano nijedan, pa je referenca nepromijenjena.
- Pozicija, `R` i `L` su relativni kao kod izvora; scena kuće ih računa iz metara, a brzinu iz m/s dijeljenjem
  s veličinom ćelije `h` (D4: solver radi u jedinicama ćelije).
- Zadano u sceni kuće: 3 m/s i odziv 4/s, izabrano vizuelno (kao uzgon u M2); panel mijenja brzinu (0 do 8 m/s)
  i odziv (0.5 do 20 /s). CFL: 3 m/s na ćeliji 0.075 m je 40 ćelija/s, uz `dt = 1/30` s to je 1.3 ćelije po
  koraku, što semi-Lagrangeova advekcija podnosi (odjeljak 4).

### Zadane vrijednosti scene kuće (D22, izabrano vizuelno 2026-11-21, poslije Erminove probe)
Prva verzija je koristila parametre podešene na 64³ (odjeljak 3 gore). U kući od 160 ćelija dim se tada širio
presporo i bio je slabo vidljiv ispred svijetlih zidova. Zato scena kuće (samo ona) mijenja:

| Parametar | Zadano (64³) | Kuća | Zašto |
| :--- | ---: | ---: | :--- |
| `buoyancyBeta` | 2.0 | 6.0 | brži uspon; solver radi u ćelijama (D4), a kuća ima 40 ćelija visine i 160 dužine |
| `dt` | 1/60 s | 1/30 s | i dalje jedan korak po frejmu (D17), ali dvostruko više simuliranog vremena po frejmu |
| jačina izvora (gustina/s) | 10 | 25 | gušći dim |
| apsorpcija prikaza | 6 | 10 | gušći izgled (samo prikaz) |
| boja dima, ambijent, svjetlo, sjena | 0.85, 0.12, 0.75, 1.0 | 0.80, 0.30, 1.0, 1.6 | osvijetljen vrh i tamnije jezgro: dim ima strukturu i vidi se ispred svijetlih i obojenih zidova (samo prikaz) |
| koraka zrake / prema svjetlu | 96 / 6 | 192 / 8 | kuća je 12 m duga; 96 koraka bi uzorkovalo svakih ~12 cm, grublje od ćelije 7.5 cm (samo prikaz) |

Prva verzija (isti dan) je imala tamnosiv dim (0.30) i apsorpciju 12; Ermin je tražio da boje dima budu bolje
vidljive. Kartica Prikaz ima i brze izglede "Svijetli dim", "Tamni dim" i "Bijela para".

Snimak poslije 15 s pokazuje dim koji se digne do plafona i širi se ispod njega kao topli sloj. Sve se mijenja u
panelu (kartice Simulacija, Izvori i Prikaz). Referenca solvera (`SmokeParams{}`) se ne mijenja.
- Testovi: `test_forces.cpp` (sila samo u cilindru, slabljenje duž ose, kosi smjer, cilj se dostigne i ne
  prebaci, brži zrak se ne koči), `test_parallel_solver.cpp` (s ventilatorima i izvorima `CpuParallel` =
  `Serial` = referentni korak po fazama).

---

## 4. Advekcija (semi-Lagrangian)

Ideja [Stam99]: ne pomjeramo gustoću naprijed, nego pitamo **odakle je zrak došao**.
Za ćeliju na poziciji `x` pogledamo gdje je bila čestica prije `dt` i uzmemo staru vrijednost
s tog mjesta:

```
ρ_novo(x) = ρ_staro( x - dt · u(x) )
```

Mjesto `x - dt·u` rijetko padne tačno u centar ćelije, pa se vrijednost interpolira iz 8
okolnih ćelija (trilinearna interpolacija). Posljedice:

- Nova vrijednost je uvijek **prosjek starih** (težine su nenegativne i daju 1), pa simulacija
  **ne može eksplodirati ni za velik `dt`**. To je pogodno za provjeru: nova vrijednost mora ležati
  između najmanje i najveće stare u okolini.
- Cijena je **blago razmazivanje** dima (numerička difuzija).
- Ako `x - dt·u` izađe izvan kutije, pozicija se ograničava (clamp). **Tačno pravilo** (kao `advect` u
  [Stam03], i kao sječenje putanje na zidu u [Fedkiw01], sl. 2): unutrašnja ćelija `i` pokriva
  `[i-0.5, i+0.5]`, pa kutija ide od `0.5` do `n+0.5`, i koordinata se ograničava na `[0.5, n+0.5]` po
  svakoj osi. Interpolacija tada smije čitati i ghost ćelije, zato one moraju biti ažurne (odjeljak 6).
- Tačka unazad se računa jednim Eulerovim korakom `x - dt·u(x)`, kao u [Stam03]. [Stam99] koristi RK2,
  što je kasnija opcija za kvalitet.
- Isti postupak koristimo za brzinu (`u`, `v`, `w` se advektiraju poljem brzine) i za `T`.
  Brzinu advektiramo iz **starog** polja, pa nema ovisnosti o redoslijedu ćelija.

Pratimo i `max|u| · dt / h` (CFL broj). Stabilnost ne ovisi o njemu, ali tačnost opada kad je
veći od oko 1, pa ga ispisujemo kao dijagnostiku.

---

## 5. Projekcija (zašto je najskuplja)

Zrak se ne može stisnuti: u svaku ćeliju koliko uđe, toliko izađe. Matematički,
**divergencija brzine je nula**: `∇·u = 0`. Poslije sila i advekcije to ne vrijedi.
Popravljamo brzinu u tri koraka:

1. **Divergencija** svake ćelije, iz razlika brzina susjeda (centralne razlike):
   `div(i,j,k) = ( u(i+1) - u(i-1) + v(j+1) - v(j-1) + w(k+1) - w(k-1) ) / (2h)`
2. **Pritisak** iz Poissonove jednadžbe `∇²p = div` rješavamo iterativno. Jedna Jacobi
   iteracija uzima prosjek 6 susjeda pritiska i oduzima divergenciju:
   `p_novo(i,j,k) = ( p(i±1) + p(j±1) + p(k±1) - div(i,j,k) ) / 6`
   (predznak i faktor `h²` zavise od tačne konvencije [Stam03]; **usvajamo onu iz `project()` u
   [Stam03] i provjeravamo je testom iz VERIFICATION.md, T3**). Radi se 20 do 80 prolaza po koraku.
3. **Korekcija brzine** gradijentom pritiska: `u ← u - ∇p`.

**Napomena o `Δt` (provjereno u radovima 2026-11-09):** [Fedkiw01], jednačine (4) i (5), pišu
`∇²p = (1/Δt)·∇·u*` i `u = u* - Δt·∇p`. Mi koristimo oblik iz [Stam03] i [Stam99] (dodatak B), bez `Δt`.
Oba oblika daju istu brzinu; naš `p` je samo `Δt·p` iz [Fedkiw01], dakle nije fizički pritisak.
Predznak: [Stam03] ima `div = -0.5·h·(...)` i `p = (div + Σ)/4`; naš `div` ima suprotan znak, pa je
`p = (Σ - div)/6` ista jednačina.

**Početna vrijednost:** kao u [Stam03], pritisak kreće od `p = 0` u svakom koraku. Kretanje od prošlog `p`
("topli start") brže konvergira, ali mijenja rezultate; to je kandidat za M4b.

Razlog zašto je ovo najskuplje: 20 do 80 prolaza kroz **cijelu** mrežu po koraku, a svaki
prolaz čita oko 7 vrijednosti da bi upisao jednu (memorija je usko grlo, ne računanje).

### Jacobi i red-black Gauss-Seidel
- **Jacobi:** čita **staro** polje, piše **novo** (dva niza). Potpuno paralelno, bez trke
  podataka, ali sporije konvergira. **Počinjemo s njim** (tako piše u planu).
- **Red-black Gauss-Seidel:** ažurira na mjestu, prvo "crne" pa "bijele" ćelije (šahovska
  ploča). Brža konvergencija, bez trke podataka. Faza 5 plana.
- Multigrid / konjugirani gradijent: napredna opcija, daleko kasnije.

### Važna napomena o tačnosti (nije obavezno očigledna)
Na mreži gdje su sve veličine u centru ćelije i koristimo centralne razlike, divergencija
**poslije korekcije ne postaje tačno nula, čak ni s potpuno konvergiranim Jacobijem**. Razlog
je što je operator "gradijent pa divergencija" koji koristimo nešto drugačiji od sedmotočkastog
Laplasijana kojim rješavamo pritisak. Zato naša provjera nije "max|∇·u| == 0", nego:

- divergencija poslije korekcije mora biti **znatno manja** nego prije (omjer ispod praga koji
  određujemo mjerenjem),
- i mora **opadati** kad povećavamo broj Jacobi iteracija.

Ovo je namjerna korekcija formulacije "mora biti blizu nule" iz našeg planskog dokumenta. Prag
određujemo u fazi 2 na stvarnim podacima, ne pogađamo ga unaprijed.

(Alternativa je tzv. MAC mreža, gdje su brzine na stranicama ćelija i divergencija postaje
tačna. Zasad je ne koristimo jer plan i literatura koriste vrijednosti u centru ćelije.)

### Koliko daleko stigne K iteracija (P2, izmjereno 2026-11-11)

**Šta je mjereno.** `smoke_bench --convergence` (PC-2, g++ 16.2.0, Release, zadani scenarij, dt = 1/60).
Za svaku mrežu i svaki ε strujanje se razvije 99 koraka sa zadanim K = 40, pa se **stoti korak** ponovi za
svaki K iz istog stanja. Zato svi redovi jedne mreže imaju isti ulaz (ista divergencija prije projekcije) i
mogu se direktno porediti. Kolone:

- `max|div|` prije i poslije projekcije, i `rms` (korijen srednjeg kvadrata) poslije;
- **ostatak** `max|∇²p − div|`: koliko je Poissonova jednačina riješena (0 = potpuno). `∇²p` je isti
  sedmotočkasti operator koji Jacobi rješava (zbir 6 susjeda − 6p).

Sirovi ispis: `perf/convergence/2026-11-11_last_step.csv`.

**ε = 5 (zadano):**

| Mreža | K | max\|div\| prije | max\|div\| poslije | poslije / prije | rms poslije | ostatak |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| 32³ | 10 | 1.826 | 0.987 | 0.541 | 0.02895 | 0.0527 |
| 32³ | 20 | 1.826 | 0.959 | 0.525 | 0.02870 | 0.0143 |
| 32³ | 40 | 1.826 | 0.950 | 0.520 | 0.02867 | 0.0039 |
| 32³ | 80 | 1.826 | 0.948 | 0.519 | 0.02866 | 0.0012 |
| 32³ | 160 | 1.826 | 0.947 | 0.519 | 0.02866 | 0.0005 |
| 32³ | 1000 | 1.826 | 0.947 | 0.519 | 0.02866 | 0.0001 |
| 64³ | 10 | 4.576 | 2.206 | 0.482 | 0.03517 | 0.399 |
| 64³ | 20 | 4.576 | 2.031 | 0.444 | 0.03281 | 0.180 |
| 64³ | 40 | 4.576 | 1.941 | 0.424 | 0.03186 | 0.0796 |
| 64³ | 80 | 4.576 | 1.900 | 0.415 | 0.03152 | 0.0401 |
| 64³ | 160 | 4.576 | 1.882 | 0.411 | 0.03139 | 0.0186 |
| 64³ | 1000 | 4.576 | 1.870 | 0.409 | 0.03128 | 0.0027 |
| 128³ | 10 | 5.100 | 2.915 | 0.571 | 0.03310 | 0.446 |
| 128³ | 20 | 5.100 | 2.872 | 0.563 | 0.03233 | 0.303 |
| 128³ | 40 | 5.100 | 2.836 | 0.556 | 0.03166 | 0.210 |
| 128³ | 80 | 5.100 | 2.798 | 0.549 | 0.03099 | 0.141 |
| 128³ | 160 | 5.100 | 2.766 | 0.542 | 0.03040 | 0.0939 |
| 128³ | 1000 | 5.100 | 2.732 | 0.536 | 0.02958 | 0.0222 |

**ε = 0 (bez vorticity confinementa):**

| Mreža | K | max\|div\| prije | max\|div\| poslije | poslije / prije | rms poslije | ostatak |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| 32³ | 10 | 0.424 | 0.160 | 0.376 | 0.00583 | 0.0534 |
| 32³ | 20 | 0.424 | 0.130 | 0.307 | 0.00482 | 0.0258 |
| 32³ | 40 | 0.424 | 0.120 | 0.283 | 0.00433 | 0.0118 |
| 32³ | 80 | 0.424 | 0.117 | 0.277 | 0.00410 | 0.0067 |
| 32³ | 160 | 0.424 | 0.117 | 0.275 | 0.00397 | 0.0036 |
| 32³ | 1000 | 0.424 | 0.117 | 0.276 | 0.00380 | 0.0004 |
| 64³ | 10 | 0.583 | 0.260 | 0.447 | 0.01004 | 0.174 |
| 64³ | 20 | 0.583 | 0.196 | 0.337 | 0.00804 | 0.112 |
| 64³ | 40 | 0.583 | 0.154 | 0.264 | 0.00628 | 0.0673 |
| 64³ | 80 | 0.583 | 0.130 | 0.222 | 0.00495 | 0.0375 |
| 64³ | 160 | 0.583 | 0.117 | 0.201 | 0.00398 | 0.0216 |
| 64³ | 1000 | 0.583 | 0.108 | 0.185 | 0.00260 | 0.0029 |
| 128³ | 10 | 0.734 | 0.422 | 0.576 | 0.01470 | 0.309 |
| 128³ | 20 | 0.734 | 0.355 | 0.483 | 0.01364 | 0.267 |
| 128³ | 40 | 0.734 | 0.293 | 0.399 | 0.01217 | 0.215 |
| 128³ | 80 | 0.734 | 0.239 | 0.326 | 0.01029 | 0.151 |
| 128³ | 160 | 0.734 | 0.197 | 0.269 | 0.00822 | 0.0964 |
| 128³ | 1000 | 0.734 | 0.150 | 0.204 | 0.00326 | 0.0237 |

**Šta tabela kaže.**

1. **Ostatak opada sporije na većoj mreži.** Za K = 40 ostatak je 0.004 (32³), 0.08 (64³) i 0.21 (128³).
   To je poznato svojstvo Jacobija: broj iteracija potreban za istu tačnost raste s kvadratom broja ćelija po osi.
   Na 128³ Poissonova jednačina sa K = 40 **nije riješena**, a nije ni sa K = 1000 (ostatak 0.022).
2. **Sa ε = 5 to skoro ne mijenja divergenciju.** Na 128³ je `max|div|` poslije projekcije 2.84 za K = 40 i 2.73
   za K = 1000 (3.7% manje; rms 6.6% manje). Čak i K = 10 daje 2.91. Divergencijom vlada "pod" projekcije na
   kolociranoj mreži (napomena gore), a ne nedovršeni Jacobi. Isto vrijedi na 64³ (1.94 prema 1.87) i 32³.
3. **Sa ε = 0 K = 40 ne dostiže pod na 64³ i 128³.** Na 128³: 0.293 za K = 40 prema 0.150 za K = 1000
   (1.95 puta više; rms 3.7 puta više), a ni K = 1000 još nije konvergirao. Na 64³: 0.154 prema 0.108.
   Na 32³ je pod dostignut već sa K = 40 (0.120 prema 0.117).
4. Preostala divergencija (`max|div|` poslije) je uz ε = 5 oko 6 do 18 puta veća nego uz ε = 0, zavisno od
   mreže i K (npr. 128³: 9.7 puta za K = 40, 18 puta za K = 1000).

**Drugi način: isti K u svih 100 koraka** (`--k-all-steps`; sirovi ispis
`perf/convergence/2026-11-11_all_steps.csv`). Ovdje se s K mijenja i samo strujanje, pa "prije" nije isto u
svim redovima; ovo pokazuje kako izgleda cijela simulacija vođena s tim K. Ćelija: `max|div|` poslije / rms poslije.

| K | 32³, ε = 5 | 64³, ε = 5 | 128³, ε = 5 | 32³, ε = 0 | 64³, ε = 0 | 128³, ε = 0 |
| ---: | :--- | :--- | :--- | :--- | :--- | :--- |
| 10 | 1.107 / 0.0298 | 2.644 / 0.0437 | 3.489 / 0.0451 | 0.192 / 0.0103 | 0.430 / 0.0240 | 0.965 / 0.0366 |
| 20 | 0.992 / 0.0289 | 2.366 / 0.0377 | 2.932 / 0.0351 | 0.135 / 0.0059 | 0.229 / 0.0125 | 0.507 / 0.0220 |
| 40 | 0.950 / 0.0287 | 1.941 / 0.0319 | 2.836 / 0.0317 | 0.120 / 0.0043 | 0.154 / 0.0063 | 0.293 / 0.0122 |
| 80 | 0.936 / 0.0286 | 1.874 / 0.0305 | 2.728 / 0.0303 | 0.117 / 0.0039 | 0.125 / 0.0036 | 0.216 / 0.0063 |
| 160 | 0.931 / 0.0286 | 1.861 / 0.0301 | 2.597 / 0.0297 | 0.117 / 0.0038 | 0.115 / 0.0027 | 0.182 / 0.0033 |
| 1000 | 0.928 / 0.0286 | 1.860 / 0.0299 | 2.470 / 0.0297 | 0.117 / 0.0038 | 0.111 / 0.0024 | 0.164 / 0.0016 |

Red K = 40 je isti kao u prvom načinu (isti račun), što je i provjera da oba načina rade isto.

- **ε = 5:** od K = 40 naviše dobitak je mali: na 128³ `max|div|` pada s 2.84 na 2.47 (13%), a rms s 0.0317 na
  0.0297 (6%); na 64³ 4% i 6%. Ispod K = 40 je primjetno lošije: K = 10 na 128³ daje rms 0.0451 (42% više nego
  K = 40). Dakle K = 40 je razuman izbor, a manji K nije besplatan, iako je u jednom koraku tako izgledalo.
- **ε = 0:** K = 40 je daleko od poda na 128³: `max|div|` 0.293 prema 0.164 (1.8 puta), rms 0.0122 prema 0.0016
  (7.4 puta). Na 64³ rms 0.0063 prema 0.0024 (2.6 puta).

**Posljedica za odluku D15** (odlučuje se na tački G1): za zadani scenarij (ε = 5) brži rješavač bi malo
popravio kvalitet, jer K = 40 već stoji blizu poda (na 128³ do 13% po max i 6 do 7% po rms; na 64³ oko 4% i 6%).
Bez confinementa (ε = 0) na 128³ K = 40 ne dostiže pod, pa bi tu rješavač koji brže konvergira (red-black
Gauss-Seidel) dao mjerljivo manju divergenciju za isti broj iteracija.

**Odluka D15 (2026-11-11, Ermin, tačka G1): Jacobi ostaje.** Red-black Gauss-Seidel eksperiment se ne radi.
Razlog: zadani scenarij koristi ε = 5 (odluka D9), a tu je K = 40 već blizu poda. Ako slučaj ε = 0 kasnije
postane važan, tabela iznad je polazna tačka za ponovnu odluku.

### 5.1 Topli start i red-black SOR (odluka D20, 2026-11-17)

D20 zamjenjuje D15 za vizualizaciju (zadana konfiguracija `SmokeParams{}` je i dalje Jacobi od `p = 0`, referenca).

**Topli start.** Pritisak se mijenja sporo od koraka do koraka, pa je pritisak prošlog koraka mnogo bolji početak
od nule: rješavač kreće skoro od rješenja. To odstupa od [Stam03] (`p = 0` svaki korak), ali jednačina je ista;
mijenja se samo početna tačka iteracije, pa se mijenja i rezultat (nije bit po bit isti).

**Red-black Gauss-Seidel / SOR.** Formula za ćeliju je ista kao kod Jacobija, `gs = (zbir 6 susjeda − div)/6`,
ali se ćelija mijenja u mjestu i odmah koristi novu vrijednost susjeda. Ćelije se boje kao 3D šahovska tabla
(`(i + j + k)` parno = crvena, neparno = crna): sve crvene imaju samo crne susjede, pa se mogu računati istovremeno
(niti, SIMD), zatim sve crne. SOR pomjera ćeliju dalje od Gauss-Seidela: `p ← p + ω (gs − p)`, `1 < ω < 2`.
U [Stam03] je `project()` zapravo Gauss-Seidel u redoslijedu petlje (mijenja `p` u mjestu); red-black je isti
postupak u redoslijedu koji se može podijeliti. Izvor za izbor rješavača kod [Fedkiw01]: TODO(paper: Fedkiw et al. 2001, odjeljak 4).

**Kako se mjeri kvalitet.** `max|div|` poslije projekcije gotovo se ne mijenja s rješavačem ni s K (64³ oko 2.0,
128³ oko 2.4 za sve varijante, 100 koraka). Razlog je mreža: divergencija i gradijent su centralne razlike preko
dvije ćelije, a rješavamo kompaktni 7-tačkasti Laplasijan, pa ni tačno rješenje ne daje `div = 0` (vidi pod u
tabeli iznad). Zato se rješavači porede po **reziduumu Poissonove jednačine** `max|∇²p − div|` (nova
`Diagnostics::poissonResidual`) i po rms divergencije (`rmsDivAfter`). Mjereno `smoke_bench --quality`, 6 niti,
60 + 30 koraka (PERF_LOG.md, P-005):

| Rješavač | reziduum 64³ | reziduum 128³ | ms/korak 64³ | ms/korak 128³ |
| :--- | ---: | ---: | ---: | ---: |
| Jacobi K = 40, od nule (referenca) | 0.050 | 0.132 | 2.59 | 30.1 |
| Jacobi K = 40, topli start | 0.0026 | 0.0054 | 2.60 | 30.5 |
| Jacobi K = 10, topli start | 0.028 | 0.034 | 1.59 | 22.0 |
| red-black GS K = 40, od nule | 0.036 | 0.125 | 5.55 | 56.0 |
| red-black SOR ω = 1.5, K = 6, topli start | 0.015 | 0.026 | 1.90 | 24.9 |

Duži razvoj (300 + 60 koraka, dim puni kutiju): referenca 0.172 / 2.18, topli Jacobi K = 10 0.235 / 1.01,
red-black SOR ω = 1.5, K = 6, topli 0.078 / 0.691. rms divergencije je za sve varijante isti ili manji od reference.
Napomena: najveći reziduum odmah poslije red-black iteracije sjedi na crvenim ćelijama, pa je za poređenje na
istom polju bolja greška prema tačnom rješenju (test `D20 red-black converges faster ...`: poslije 40 iteracija
Jacobi 0.048, GS 0.030, SOR 1.8 0.0013).

**Izbor za vizualizaciju (Ermin, 2026-11-17, CPI prije brzine):** red-black SOR, ω = 1.5, K = 6, topli start,
niti koje spavaju. Manji reziduum od reference i najniži CPI od svih izmjerenih varijanti (PERF_LOG.md, P-005).
Veći ω (1.8, 1.9) s toplim startom daje veći najveći reziduum (prebacivanje), pa nije izabran.

---

## 6. Rubni uslovi (zidovi kutije)

Prema planu i [Stam03] (`set_bnd`):
- **Brzina:** komponenta okomita na zid je nula (nema protoka kroz zid).
- **Gustoća i pritisak:** kopiraju se iz najbliže unutrašnje ćelije (nulti gradijent).
- Ghost sloj čuva ove vrijednosti, pa unutrašnje petlje nemaju posebne slučajeve.
- **Ivice i uglovi u 3D:** `set_bnd` u [Stam03] je 2D i uglove popunjava prosjekom. U 3D trilinearna
  interpolacija čita i ghost ivice i uglove. Zato `setBoundary` ide po osama redom (x, pa y, pa z), a svaki
  sljedeći prolaz pokriva i ghost redove prethodnih osa. Ivice i uglovi tako dobiju vrijednost istim pravilom
  (kopija ili promjena znaka), bez prosjeka. Ovo je namjerno odstupanje od [Stam03].
- Pravilo u kodu: svaka faza koja upiše polje na kraju poziva `setBoundary` za to polje, pa je ghost sloj
  uvijek ažuran za sljedeću fazu.

**Odluka D3 (donesena: zatvoren vrh u serijskoj fazi):** treba li vrh kutije biti otvoren da dim može izaći? Zatvorena kutija je
jednostavnija i dobra za testove; otvoren vrh je ljepši za demo. Preporuka: zatvorena u
serijskoj fazi, otvoren vrh kao opcija kasnije.

---

## 7. Difuzija i viskoznost

Kod dima su mala i često se preskaču (`viscosity = 0`, `diffusion = 0`). [Stam03] implicitno
rješava difuziju Gauss-Seidelom jer je eksplicitna verzija nestabilna. Zasad je ne
implementiramo; parametar ostaje u strukturi da kasnije ne mijenja interfejs.

---

## 8. Mjerenje: kamo to vodi (Amdahl)

Ubrzanje cijelog programa ograničava dio koji ne ubrzamo [Amdahl67]:

```
S(P) = 1 / ( (1 - f) + f / P )
```
`f` je dio vremena koji se može paralelizirati, `P` broj niti. Primjer iz našeg plana: ako
pritisak troši 80% vremena i savršeno se paralelizira (`f = 0,8`), na 8 niti je najviše
`S = 1 / (0,2 + 0,1) ≈ 3,3`. **Stvarno `f` mjerimo** (faza 2 plana), ne pretpostavljamo.
Uz to računamo i propusnost memorije (GB/s) jer je Jacobi ograničen memorijom (VERIFICATION.md, odjeljak 4).
