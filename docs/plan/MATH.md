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
- `h` je dužina stranice ćelije. **Preporuka (odluka D4): radimo u jedinicama ćelije, dakle
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

**Odluka D1:** naš plan navodi samo gustoću, brzinu i pritisak, a rečenica "topao i rijedak zrak
se diže" traži temperaturu. Preporuka: uvesti polje `T` (jedno polje više) jer daje
vjerodostojnije kovitlanje, uz mogućnost da se `β = 0` pa se model svede na gustoću.
Vrijednosti `α`, `β` biramo eksperimentalno, ne prepisujemo ih iz rada.

### Vorticity confinement (opcionalno)
Numerička difuzija izgladi sitne vrtloge i dim izgleda "mrtvo". [Fedkiw01] (jednadžbe 9 do 11)
vraća ih silom:

```
ω   = ∇ × u                  (vorticity, "koliko se zrak vrti")
N   = ∇|ω| / |∇|ω||           (smjer prema jačoj vrtložnosti)
f_c = ε · h · (N × ω)         (ε je parametar; ε = 0 isključuje)
```

Ide u fazu 3 plana, ne u prvu verziju.

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
- Ako `x - dt·u` izađe izvan kutije, pozicija se ograničava (clamp) na unutrašnjost.
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

**Odluka D3:** treba li vrh kutije biti otvoren da dim može izaći? Zatvorena kutija je
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
