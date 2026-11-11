# OBSTACLES: prepreke u kutiji s dimom (M-OBS)

Ovaj dokument objašnjava kako dim "vidi" predmete u kutiji: kako se mesh pretvara u ćelije, šta se s tim
ćelijama radi u koraku simulacije i zašto baš tako. Radni plan: [EXECUTION_PLAN.md](EXECUTION_PLAN.md), faza P4.
Odluka o dizajnu: D16 (PLAN.md). Kod: `Solver/include/smoke/Obstacles.h`, `ObjLoader.h`,
`Solver/src/Voxelizer.cpp`, `Solver/src/stages/Obstacles.cpp`, testovi u `Solver/tests/test_obstacles.cpp`.

Stanje (2026-11-11): solver dio je urađen i testiran. Spajanje s rendererom (učitavanje `.obj` iz menija, crtanje
mesha, zaklanjanje dima) je sljedeći korak.

---

## 1. Ideja u tri rečenice

1. Svaka ćelija mreže dobije **tip**: Fluid (zrak i dim), Solid (dio prepreke) ili Porous (propušta, ali koči).
2. Iz tih tipova se **jednom** naprave kratke liste: koje su ćelije čvrste, koje čvrste dodiruju fluid i koje su porozne.
3. Vektorizirana jezgra (Jacobi, advekcija, sile) ne znaju za prepreke i rade nad svim ćelijama kao prije; poslije
   njih kratke petlje prođu samo kroz liste i poprave vrijednosti u ćelijama prepreke.

**Zašto liste, a ne `if` u jezgrima (odluka D16).** `if (ćelija je čvrsta)` u unutrašnjoj petlji bi pokvario
vektorizaciju koju smo mjerili u M4 i koštao bi svaku ćeliju, i kad prepreka nema. Lista graničnih ćelija je mala
(površina predmeta, ne zapremina kutije), pa je trošak srazmjeran veličini prepreke. Bez prepreka u koraku ostaje
jedan `if` po fazi, izvan svih petlji, i rezultat je **bit po bit isti** kao prije (provjera u odjeljku 6).

---

## 2. Od mesha do ćelija

### 2.1 Učitavanje `.obj`

`smoke::loadObj` čita samo geometriju: linije `v x y z` (vrhovi) i `f ...` (plohe). Ploha s više od tri vrha se
dijeli u lepezu trouglova oko prvog vrha. Prihvata oblike `v/vt/vn` i negativne indekse (brojanje unazad od
zadnjeg pročitanog vrha). Normale, teksture i materijale preskače. Parser je naš, od stotinjak linija, da Solver
i dalje nema nijednu vanjsku biblioteku.

### 2.2 Koordinate

Mesh se solveru daje u **koordinatama kutije**: x, y i z od 0 do 1 duž svake ose, isto kao izvor dima
(`SmokeSource`). Solver ne zna gdje je kutija u sceni; pomjeranje, skaliranje i rotaciju predmeta radi strana
renderera i solveru preda već preračunate vrhove. Dio mesha izvan kutije se zanemaruje.

### 2.3 Voxelizacija: površina

"Voxelizacija" znači odrediti koje ćelije mesh zauzima. Prvi korak: za svaki trougao nađemo ćelije kroz koje
prolazi. Ćelija je kocka, pa je pitanje "da li trougao dodiruje kocku?".

Odgovor daje **teorem o razdvajajućoj osi** (Akenine-Möller, 2001): dva konveksna tijela se **ne** dodiruju tačno
onda kad postoji osa na kojoj se njihove "sjene" (projekcije) ne preklapaju. Za trougao i kocku dovoljno je
probati 13 osa: 3 ose kocke, normalu trougla i 9 vektorskih proizvoda (osa kocke × ivica trougla). Ako nijedna
ne razdvaja, dodiruju se.

Ovakva voxelizacija je **konzervativna**: označi svaku ćeliju koju površina makar okrzne. Zato je voxelizirani
predmet malo deblji od pravog, za oko pola do jedne ćelije.

### 2.4 Voxelizacija: unutrašnjost ("poplava")

Površina je samo ljuska. Unutrašnjost nalazimo **poplavom izvana**: krenemo od sloja ćelija oko kutije (sigurno
je vani) i širimo se na susjede koji nisu površina. Sve do čega poplava ne dođe, zatvoreno je površinom, dakle je
unutra.

Ako mesh ima rupu, poplava uđe kroz nju i unutrašnjosti nema; ostaje samo ljuska. To nije greška nego svojstvo:
funkcija vraća `closed = false`, pa pozivalac zna.

### 2.5 Tri načina (`ObstacleMode`)

| Način | Šta označi | Za šta |
| :--- | :--- | :--- |
| `Solid` | površinu i unutrašnjost, kao Solid | puni predmeti (kugla, kocka); traži zatvoren mesh |
| `Shell` | samo površinu, kao Solid | tanke stvari i otvoreni meshevi (ploča, posuda) |
| `Porous` | kao `Solid`, ali ćelije postaju Porous | deka, mreža, filter; otvorena ploha daje samo svoju površinu |

`shellHalfThickness` zadaje debljinu ljuske: 0.5 označi samo ćelije koje površina dodiruje, 1.0 daje ljusku
debelu najmanje dvije ćelije (vidi "Poznata slabost" u odjeljku 5).

### 2.6 Izmjereno (2026-11-11, 32³, g++ 16.2.0)

| Predmet | Označeno ćelija | Analitička zapremina | Odnos |
| :--- | ---: | ---: | ---: |
| Kocka stranice 15.36 ćelija | 4096 (16³) | 3623.9 | 1.13 |
| Kugla poluprečnika 8 ćelija | 2750 | 2144.7 | 1.28 |

Višak je onaj konzervativni sloj: kod kugle odgovara oko 0.7 ćelije većem poluprečniku. Na finijoj mreži isti
predmet ima relativno manji višak, jer je sloj i dalje debeo oko jedne ćelije.

---

## 3. Rijetke liste

`buildObstacleLists` (interno, `Solver/src/stages/ObstacleLists.h`) iz tipova ćelija jednom napravi:

| Lista | Šta sadrži | Ko je koristi |
| :--- | :--- | :--- |
| `solid` | sve čvrste ćelije | brisanje dima i temperature |
| `boundarySolid` | čvrste ćelije koje dodiruju bar jednu ćeliju koja nije čvrsta | pritisak, brzina, gustina uz površinu |
| `neighbour`, `neighbourAxis` | za svaku graničnu ćeliju: njeni susjedi koji nisu čvrsti i duž koje ose leže | isto |
| `porous` | sve porozne ćelije | otpor i filtriranje |

Brojevi u listama su indeksi u nizu floatova polja (`Field3D::index`), pa ista lista vrijedi za svako polje iste mreže.
Liste se prave u `SmokeSolver::setObstacles` i žive dok se prepreke ne promijene; `resize()` ih briše.

---

## 4. Fizika: šta se radi u koraku

Uzor za sve je ono što solver već radi na zidovima kutije (`setBoundary`, [Stam03] `set_bnd`): ghost ćelija iza
zida dobije vrijednost koja daje željeni uslov **na samom zidu**. Površina prepreke je isti takav zid, samo usred
mreže, a ulogu ghost ćelije igra čvrsta ćelija uz fluid.

| Veličina | Na zidu kutije (postojeće) | Na površini prepreke (novo) | Uslov koji se time dobije |
| :--- | :--- | :--- | :--- |
| Pritisak | ghost = unutrašnji susjed | čvrsta = srednja vrijednost fluidnih susjeda (`extendIntoSolids`) | ∂p/∂n = 0: pritisak ne gura kroz zid |
| Brzina, komponenta okomita na površinu | ghost = −susjed | čvrsta = −susjed (`reflectVelocityIntoSolids`) | brzina na samoj površini je 0: nema protoka kroz nju |
| Brzina, ostale komponente | ghost = susjed | čvrsta = susjed | klizanje uz površinu bez kočenja |
| Gustina i temperatura | ghost = susjed | prije advekcije: čvrsta = srednja vrijednost susjeda; poslije: 0 | dim klizi uz površinu, a u prepreci ga nema |

Čvrsta ćelija na ivici ili uglu predmeta ima više fluidnih susjeda; tada uzima srednju vrijednost onoga što svaki
susjed traži. Duboko u prepreci brzina je 0.

**Gdje u koraku** (`SmokeSolver::step`):

1. Izvor, pa brisanje dima u čvrstim ćelijama (izvor može zahvatiti prepreku).
2. Sile, pa otpor u poroznim ćelijama i brzina u čvrstim.
3. Advekcija brzine, pa opet brzina u čvrstim.
4. Projekcija: poslije **svake** Jacobi iteracije pritisak se prenese u granične čvrste ćelije; poslije oduzimanja
   gradijenta brzina u čvrstim se postavi ponovo.
5. Prije advekcije gustine: gustina i temperatura se prenesu u granične čvrste ćelije. Poslije: brisanje u čvrstim
   i filtriranje u poroznim.

**Porozne ćelije** su za pritisak obični fluid. Dobijaju silu otpora srazmjernu brzini, du/dt = −σ·u, riješenu
implicitno: `u ← u / (1 + dt·σ)`. Taj oblik ne može promijeniti smjer brzine ni "eksplodirati", ma koliki bili
`dt` i `σ`. Dim se opcionalno filtrira istim oblikom: `ρ ← ρ / (1 + dt·φ)`. Sa σ = φ = 0 porozna ćelija je
potpuno isto što i fluid (test to provjerava: ista masa do zadnjeg bita).

### Odstupanje od plana: brzina se odbija, ne postavlja na nulu

EXECUTION_PLAN.md (P4, tačka 2) kaže: "Brzina u solid ćelijama je nula". **To je prvo i urađeno, i izmjereno je
da ne radi.** Scenarij: 32³, izvor pri dnu, kugla poluprečnika 5.4 ćelije na sredini, 600 koraka.

| Pravilo za brzinu u čvrstim ćelijama | Dim pored kugle (na njenoj visini) | Dim iznad kugle |
| :--- | ---: | ---: |
| bez kugle (poređenje) | | 1207.1 |
| nula (kako piše u planu) | 12.6 | 0.0000004 |
| odbijanje, kao na zidu kutije | 822.5 | 0.026 (poslije 1200 koraka: 1226.6) |

**Zašto nula ne radi.** Na našoj mreži su sve veličine u središtu ćelije, a divergencija je centralna razlika:
`div(i) = (u(i+1) − u(i−1)) / 2`. Ako je `i+1` čvrsta ćelija s nulom, projekcija iz uslova `div(i) = 0` izvede
`u(i−1) = 0`. Time je ograničena ćelija **dvije** ćelije od prepreke, a ćelija `i`, ona koja prepreku dodiruje,
ostaje slobodna. Ona može imati brzinu pravo u prepreku, dim se tamo nagura i izgubi.

S odbijanjem je `u(čvrsta) = −u(i)`, pa je brzina na površini (sredina između te dvije ćelije) tačno 0, a ćelija
uz prepreku je vezana za svoju čvrstu susjedu. Zidovi kutije rade isto od početka, zato tamo problema nije bilo.

Ovo je izmjena u okviru D16 (liste, netaknuta jezgra), ali mijenja jednu rečenicu odobrenog plana, pa se
**potvrđuje na tački G3**. Test `smoke_goes_around_a_solid_sphere_instead_of_through_it` čuva granicu
(najmanje 100 pored kugle; staro pravilo daje 12.6).

**Masa.** U istom scenariju ukupna masa dima poslije 600 koraka je 3034 s kuglom prema 4056 bez nje (odnos 0.75).
Semi-Lagrangian advekcija ne čuva masu ni bez prepreka (test T10: oko 7% u 100 koraka), a uz prepreku se gubi još.
Koliki dio razlike otpada na samu površinu prepreke, nije razdvojeno.

---

## 5. Ograničenja i poznate slabosti

- **Tanka čvrsta ljuska.** Čvrsta ćelija koja ima fluid s obje strane (ljuska debela jednu ćeliju) dobije
  srednju vrijednost pritiska i brzine s obje strane, pa dvije strane "cure" jedna u drugu. Za tanke čvrste
  predmete treba `shellHalfThickness = 1.0` (najmanje dvije ćelije). Za porozne predmete ovo ne vrijedi, jer su
  one za pritisak fluid.
- **Stepenasta površina.** Predmet je sastavljen od cijelih ćelija; kosa ili zaobljena površina je "stepenice".
  Dim zato uz nju ne klizi savršeno glatko. Finija mreža to ublažava.
- **Predmet je deblji za oko jednu ćeliju** (konzervativna voxelizacija, odjeljak 2.6).
- **Klizanje bez trenja.** Tangencijalna brzina se kopira (kao na zidovima kutije), pa površina ne koči strujanje
  uz sebe.
- **Jedan par koeficijenata** (σ, φ) vrijedi za sve porozne ćelije u kutiji.
- **Nepomične prepreke.** Predmet koji se kreće bi tražio ponovnu voxelizaciju i brzinu površine različitu od nule.
- **Vrijednosti σ i φ nisu izmjerene ni preuzete iz literature**: to su parametri koje korisnik podešava
  (`TODO(measure)` ako zatreba fizički smisao, npr. propusnost stvarne tkanine).

---

## 6. Kako se provjerava

`Solver/tests/test_obstacles.cpp`, 10 testova (ukupno ih je sada 58):

| Test | Šta dokazuje |
| :--- | :--- |
| `obj_parser_reads_...`, `obj_parser_rejects_...` | parser: poligoni, oblici s kosom crtom, negativni indeksi; greška s brojem linije |
| `voxelized_box_and_sphere_volumes_...` | zapremina kocke i kugle prema analitičkoj (odjeljak 2.6) |
| `flood_fill_finds_the_inside_only_for_a_closed_mesh` | zatvoren mesh ima unutrašnjost, otvoren ne; načini Shell i Porous; debljina ljuske |
| `obstacle_lists_hold_...` | liste i `extendIntoSolids` na ručno zadanom primjeru |
| `solid_cells_keep_zero_velocity_and_no_smoke_...` | duboko u prepreci brzina 0; nigdje u prepreci dima; na površini normalna brzina 0; ghost slojevi ispravni |
| `smoke_goes_around_a_solid_sphere_...` | dim obilazi kuglu (odjeljak 4) |
| `a_porous_layer_slows_the_smoke_down` | masa iznad poroznog sloja poslije 300 koraka: 1007.5 bez otpora, 69.4 sa σ = 20, 0.037 uz φ = 20 |
| `obstacle_simulation_is_stable_and_deterministic` | 500 koraka bez NaN; dva pokretanja ista do bita; ispisuje hash za poređenje gradnji |
| `without_obstacles_the_result_is_bit_for_bit_unchanged` | prazna mreža prepreka i uklonjene prepreke ne mijenjaju ništa |

Uz to, bez prepreka referenca mora ostati ista do bita:

```
Solver\build\smoke_tests.exe
Solver\build\smoke_bench.exe --verify Solver\reference\serial_64_100steps_pc2.bin --grid 64 --steps 100
```

Stanje 2026-11-11: gradnja bez AVX2 prolazi 58/58 i `--verify` je bit po bit isto; s AVX2 jezgrima `--verify` je
bit po bit isto, a hash gustine s preprekama je isti u obje gradnje. Trošak prepreka po protokolu D10 **nije
izmjeren** (vidi PLAN.md, M-OBS).
