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
