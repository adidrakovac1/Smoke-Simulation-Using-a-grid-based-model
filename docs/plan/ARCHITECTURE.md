# ARCHITECTURE: arhitektura projekta simulacije dima

Ovaj dokument opisuje kako je projekat podijeljen na cjeline, šta svaka cjelina smije
a šta ne smije, i kako se spajaju. Čita se prije pisanja ijedne linije koda.

Povezani dokumenti: [PLAN.md](PLAN.md) (šta i kojim redom), [MATH.md](MATH.md) (matematika
simulacije), [VERIFICATION.md](VERIFICATION.md) (kako provjeravamo tačnost i mjerimo).

---

## 1. Osnovna ideja: tri sloja, jedan smjer ovisnosti

```
 ┌──────────────────────────────────────────────────────────────┐
 │  Integration/   (zadnji milestone, M-VIZ)                    │
 │  SmokeScene, Texture3D, VolumeRaymarchPass, ImGui panel      │
 │  Jedino mjesto koje poznaje i Solver i Renderer.             │
 └───────────────┬──────────────────────────┬───────────────────┘
                 │ uključuje                │ uključuje
                 ▼                          ▼
 ┌───────────────────────────┐   ┌──────────────────────────────┐
 │  Solver/   (NAŠ KOD)      │   │  Renderer/   (TUĐI KOD)      │
 │  Čisti C++20, bez OpenGL, │   │  OpenGL 4.6, GLFW, ImGui...  │
 │  bez GLM, bez Renderera.  │   │  Ne zna da Solver postoji.   │
 └───────────────────────────┘   └──────────────────────────────┘
```

**Pravila (ne krše se bez dogovora cijelog tima):**

1. `Solver/` ne uključuje nijedan header iz `Renderer/`, OpenGL-a, GLFW-a ni GLM-a. Zavisi samo
   od standardne biblioteke. Zato se može graditi, testirati i mjeriti na bilo kojem računaru,
   čak i bez grafičke kartice.
2. `Renderer/` ne uključuje ništa iz `Solver/`. Renderer ne zna da dim postoji.
3. Samo `Integration/` uključuje oba. To je jedino mjesto gdje se spajaju.
4. Izmjene u `Renderer/` su dozvoljene samo na mjestima navedenim u odjeljku 6.

**Zašto ovako?**
- Renderer je tuđ kod (prethodni tim), velik (oko 16 000 linija) i ima svoja pravila. Ako ga
  ne diramo, nema konflikata pri spajanju i nema rizika da nešto pokvarimo.
- Mjerenja ubrzanja moraju biti čista. Ako solver radi u konzolnom programu bez prozora,
  vrijeme nije zagađeno crtanjem.
- OpenMP, SIMD i kasnije GPU mijenjaju samo `Solver/`. Renderer ostaje isti.
- Svako od troje može raditi na svom dijelu bez čekanja drugih.

---

## 2. Struktura foldera

```
Smoke-Simulation-Using-a-grid-based-model/
├── CLAUDE.md                  pravila za Claude CLI (čita ga automatski)
├── README.md
├── docs/
│   ├── *.pdf                  literatura (već postoji)
│   ├── Windows_Setup_Uputstvo.pdf
│   ├── plan/                  ovi dokumenti (uključujući OPTIMIZATION.md i PERF_LOG.md)
│   └── reference/
│       └── PRS_Vektorizacija/ laboratorijski primjer vektorizacije (tuđi/stari kod, samo uzor)
│
├── perf/
│   └── results.csv            strojno čitljiva evidencija mjerenja (PERF_LOG.md)
│
├── Renderer/                  TUĐI KOD, ne diramo (osim odjeljka 6)
│
├── Solver/                    NAŠ KOD: simulacija dima (skelet postoji, vidi Solver/README.md)
│   ├── CMakeLists.txt         samostalan projekat, ne zavisi od Renderera
│   ├── README.md              kako se gradi i pokreće
│   ├── include/smoke/         javni headeri (ono što vide drugi)
│   │   ├── GridSize.h
│   │   ├── Field3D.h
│   │   ├── SmokeParams.h
│   │   ├── SolverConfig.h
│   │   ├── SmokeSolver.h      jedina klasa koju Integration poziva
│   │   ├── DensityView.h
│   │   ├── Diagnostics.h
│   │   └── StepTimings.h
│   ├── src/
│   │   ├── SmokeSolver.cpp
│   │   ├── SmokeState.h       interno (sva polja), nije javno
│   │   ├── Profiler.h         ScopedTimer
│   │   ├── Field3D.cpp
│   │   └── stages/            jedna datoteka po fazi koraka
│   │       ├── Stages.h            deklaracije faza, SlabRange
│   │       ├── Sources.cpp
│   │       ├── Forces.cpp          uzgon i vorticity confinement
│   │       ├── Advection.cpp       semi-Lagrangian
│   │       ├── Projection.cpp      divergencija, Jacobi, korekcija brzine
│   │       └── Boundary.cpp        rubni uslovi (set_bnd)
│   ├── apps/smoke_cli/main.cpp    konzolni program (bez prozora)
│   ├── tests/                     provjere tačnosti (doctest, odluka D5)
│   ├── third_party/doctest/       doctest.h, zapakovan u repozitorij (odluka D5)
│   ├── bench/                     mjerenje vremena po fazama (zasad prazan stub)
│   └── reference/                 referentni rezultati za poređenje verzija (kasnije)
│
└── Integration/               TEK U ZADNJEM MILESTONE-U (M-VIZ): spoj Solvera i Renderera
    ├── CMakeLists.txt
    ├── include/ i src/
    │   ├── SmokeScene.*           Scene koja drži SmokeSolver
    │   ├── Texture3D.*            GL 3D tekstura (Renderer je nema)
    │   ├── VolumeRaymarchPass.*   crtanje dima
    │   └── SmokeUI.*              ImGui kontrole
    └── shaders/volume_raymarch.{vert,frag}
```

---

## 3. Podaci: kako čuvamo mrežu

### 3.1 Veličina mreže se mijenja u toku rada

Veličina nije zalijepljena u kod. Postoji struktura:

```cpp
struct GridSize { int nx = 64, ny = 64, nz = 64; };   // unutrašnje ćelije
```

- **Zadano 64³** (262 144 ćelija). Za testiranje i razvoj.
- **128³** (2 097 152 ćelija) za ozbiljnija mjerenja. Moguće i 32³, 96³, ili nekubično (npr. 64×128×64).
- Mijenja se na tri načina:
  1. u konzolnom programu parametrom: `smoke_cli --grid 128` ili `--nx 64 --ny 128 --nz 64`,
  2. u kodu pozivom `solver.resize(GridSize)`,
  3. u rendereru, u ImGui panelu (padajući izbornik 32 / 64 / 96 / 128 i dugme **Apply**).
- `resize()` ponovo alocira sva polja i resetuje simulaciju. Zbog toga je u meniju dugme
  Apply, a ne klizač koji bi alocirao memoriju svakog frejma.

Memorija (procjena, oko 10 polja tipa `float` po ćeliji): 64³ je oko 10 MB, 128³ oko 84 MB.

### 3.2 Raspored u memoriji

```cpp
class Field3D {
    // (nx+2) * (ny+2) * (nz+2) floatova: unutrašnje ćelije + sloj "ghost" ćelija oko njih
    float& at(int i, int j, int k);   // idx = i + sx * (j + sy * k)
};
```

- **Ghost sloj (1 ćelija oko kocke).** Isto kao u Stamovom kodu (`set_bnd`): rubne ćelije čuvaju
  vrijednosti za granične uslove, pa petlje u unutrašnjosti ne trebaju posebne `if`-ove za
  rubove. Razlog izbora: literatura koju imamo koristi baš tu konvenciju, pa je kod lako
  poredati s člankom.
- **`i` je najbrži indeks** (susjedne ćelije po x leže jedna do druge u memoriji). Kasnije
  SIMD vektorizira baš unutrašnju petlju po `i`, pa ovo ne mijenjamo.
- **Svako polje je poseban niz** (`density`, `u`, `v`, `w`, `pressure`, ...), ne struktura po
  ćeliji. Ovo se zove "structure of arrays" i bolje se vektorizira i bolje troši keš.
- **Poravnanje.** Nizovi se alociraju poravnato na 64 bajta (`std::aligned_alloc` ili vlastiti
  alokator). Za serijsku verziju ne mijenja ništa, ali kasnije SIMD to traži, a jeftino je to
  odmah uraditi kako bi se izbjegla preinaka.
- **Korak reda ("pitch").** Samo poravnat početak niza nije dovoljno: ako je red dug `nx + 2 = 66`
  floatova, red 1 ne počinje na granici od 32 bajta. Zato se širina reda (`strideJ`) zaokružuje
  na višekratnik od 8 floatova (kasnije 16 za AVX-512), a višak su "prazne" ćelije. `DensityView`
  to izražava preko `strideJ/strideK`. Razlog: lab o vektorizaciji pokazuje razliku poravnatih i
  neporavnatih učitavanja (OPTIMIZATION.md, T11). Ako ovo ne uradimo odmah, kasnije mijenja indeksiranje svuda.

### 3.3 Polja

| Polje | Šta je | Napomena |
| :--- | :--- | :--- |
| `density` ρ | koliko dima ima | ovo šaljemo rendereru |
| `temperature` T | temperatura | uzgon po Fedkiwu (vidi MATH.md, odluka D1) |
| `u`, `v`, `w` | brzina po x, y, z | |
| `pressure` p | pritisak | |
| `divergence` | pomoćno polje | za projekciju |
| `*_prev` ili scratch | druga kopija za dvostruko baferovanje | advekcija i Jacobi čitaju staro, pišu novo |

---

## 4. Javni interfejs Solvera

Sve što ostatak projekta vidi nalazi se iza `SmokeSolver`. Skica (tačni potpisi se dogovaraju u
fazi 0):

```cpp
namespace smoke {

struct SmokeParams {          // sve se može mijenjati u toku rada
    float dt = 1.0f / 60.0f;
    int   jacobiIterations = 40;     // 20 do 80 po planu
    float buoyancyAlpha = 0.f;       // koeficijent težine dima (vrijednosti se biraju vizuelno)
    float buoyancyBeta  = 0.f;       // koeficijent uzgona od temperature
    float ambientTemp   = 0.f;
    float vorticityEps  = 0.f;       // 0 = isključen vorticity confinement
    float viscosity     = 0.f;       // 0 = preskoči difuziju brzine (kod dima je mala)
    float diffusion     = 0.f;
    // izvor dima: pozicija, radijus, jačina, temperatura
};

struct SolverConfig {
    GridSize   grid;                 // zadano 64^3
    SmokeParams params;
    // Backend backend = Backend::Serial;   // kasnije: OpenMP, Simd, Gpu
};

class SmokeSolver {
public:
    explicit SmokeSolver(const SolverConfig& cfg);

    void step(float dt);                         // JEDAN korak simulacije
    void reset();                                // briše polja
    void resize(const GridSize& newSize);        // realokacija + reset
    SmokeParams& params();                       // izmjena parametara u toku rada

    DensityView  density() const;                // samo za čitanje, za renderer
    Diagnostics  diagnostics() const;            // max |div|, ukupna masa, max brzina
    const StepTimings& lastTimings() const;      // vrijeme po fazama zadnjeg koraka
};

} // namespace smoke
```

`DensityView` je tanak opis memorije koju renderer može čitati, bez kopiranja:
```cpp
struct DensityView {
    const float* origin;   // pokazivač na prvu UNUTRAŠNJU ćeliju
    int nx, ny, nz;        // unutrašnje dimenzije
    int strideJ, strideK;  // koliko floatova preskočiti za j+1 (red) i k+1 (ravan)
};
// vrijednost(i,j,k) = origin[i + strideJ*j + strideK*k]
```
Time `Integration` može poslati u GPU samo unutrašnji dio (OpenGL ima parametre
`GL_UNPACK_ROW_LENGTH` i `GL_UNPACK_IMAGE_HEIGHT` upravo za preskakanje redova), ili prvo
kopirati unutrašnjost u kompaktan bafer. Odluka je u zadnjem milestone-u (M-VIZ).

### Redoslijed unutar `step(dt)`

Slijedi plan iz našeg dokumenta ("Simulacije dima, grid model, naša zamisao"):

1. `addSources`: dodaj dim (i temperaturu) u nekoliko ćelija pri dnu.
2. Sile: `applyBuoyancy` (uzgon), opcionalno `applyVorticityConfinement`.
3. `advectVelocity`: brzina nosi samu sebe.
4. `project`: divergencija, Jacobi iteracije, korekcija brzine. **Najskuplji dio.**
5. `advectScalar` za gustoću (i temperaturu): popravljena struja nosi dim.

Svaka faza mjeri svoje vrijeme (`StepTimings`), jer prije paralelizacije moramo znati gdje
se troši vrijeme.

---

## 5. Pravila kodiranja zbog kasnije paralelizacije

Serijska verzija se piše tako da kasnije OpenMP i SIMD ne traže prepisivanje. To ne znači da
sad paraleliziramo, nego da ne ugrađujemo prepreke:

1. **Svaka faza je slobodna funkcija** u `namespace smoke::serial`, koja prima polja i
   `SlabRange{k0, k1}` (raspon slojeva po z). Serijska verzija zove s punim rasponom. OpenMP
   kasnije podijeli raspon među nitima, a nijedan algoritam se ne mijenja.
2. **Dvostruko baferovanje.** Advekcija i Jacobi čitaju iz starog polja, a pišu u novo. Tako
   nema trke podataka (jedan od ciljeva projekta). Zamjena polja je zamjena pokazivača.
3. **Bez skrivenog stanja.** Nema `static` pomoćnih nizova unutar funkcija, nema globalnih
   promjenljivih. Sve što funkcija koristi dobija kroz argumente.
4. **Unutrašnja petlja je `for (i ...)` po najbržem indeksu**, bez virtualnih poziva, bez `if`
   za rubove (za to služi ghost sloj) i bez pozivanja funkcija koje se ne mogu inlinovati.
5. **Determinizam.** Bez slučajnih brojeva u koraku. Isti ulaz daje isti izlaz (bit po bit u
   serijskoj verziji). To omogućava poređenje s paralelnim verzijama.
6. **Referentna verzija ostaje.** `smoke::serial` se ne briše kad dodamo `smoke::omp` ili
   `smoke::simd`. Ostaje kao tačna referenca za poređenje i za računanje ubrzanja.
7. **Bez `-ffast-math` i bez `-march=native` u referentnoj gradnji.** Prvo zato što mijenja
   rezultate, drugo da bi svi u timu dobili iste brojeve na različitim procesorima. SIMD faza
   uvodi vlastite oznake, i to **po fajlu** (npr. `-mavx2` samo za fajl s AVX2 jezgrima), uz provjeru
   podrške procesora u toku rada i rezervnu putanju, kao u laboratorijskom primjeru
   (`docs/reference/PRS_Vektorizacija/`). Gradnja mora biti **Release** (zadano u našem CMake-u).
8. **Dvije osnove za poređenje.** Pored referentne serijske gradnje (kompajler po zadanom) držimo i
   **B0**: ista serijska jezgra bez vektorizacije (`-fno-tree-vectorize`), kao `*_scalar.cpp` u labu.
   Ubrzanja računamo prema obje (OPTIMIZATION.md, odjeljak 2.3). Cijela procedura optimizacije
   i evidencije je u OPTIMIZATION.md i PERF_LOG.md.

---

## 6. Jedino mjesto gdje diramo Renderer

Renderer već ima ono što nam treba za crtanje dima:
- `Scene::OnPostRender()` se poziva nakon crtanja neprozirnih objekata, a prije tone mappinga
  (vidi `Application.cpp`). To je tačno mjesto iz našeg plana za volumni prolaz.
- `Scene::OnImGuiRender()` omogućava vlastiti ImGui panel (izbor veličine mreže i parametara).
- Nove scene se registruju u `test-app/src/main.cpp`.

Čega Renderer **nema** (zato ide u `Integration/`, ne u `Renderer/src`):
- **3D teksturu** (pretraga za `TEXTURE_3D` nije ništa našla). `Texture3D` pišemo sami.
- **Čitljiv depth buffer.** `HdrFramebuffer` ima depth kao renderbuffer, koji se ne može
  uzorkovati u shaderu. Posljedica: dim ne može biti zaklonjen objektima scene. Za demo u
  praznoj sceni to ne smeta. Ako zatreba, dvije su opcije (promjena depth-a u teksturu ili
  kopiranje depth-a nakon crtanja), i obje diraju Renderer. Zasad **ne radimo**.

**Dozvoljene izmjene u `Renderer/` (cijeli popis):**
1. `Renderer/test-app/CMakeLists.txt`: dodati `add_subdirectory(../../Solver ...)`,
   `add_subdirectory(../../Integration ...)` i povezati `SmokeIntegration` s `TestApp`.
2. `Renderer/test-app/src/main.cpp`: napraviti `SmokeScene` i dodati je u listu scena.

Sve ostalo ostaje netaknuto. Ako se pojavi potreba za većom izmjenom, prvo se dogovara u timu
i upisuje u ovaj odjeljak.

---

## 7. Odluke koje su donesene i koje tek treba potvrditi

| Odluka | Stanje | Obrazloženje |
| :--- | :--- | :--- |
| Solver je odvojena biblioteka bez OpenGL-a | **Donesena** | odjeljak 1 |
| Veličina mreže se mijenja u toku rada, zadano 64³ | **Donesena** | po zahtjevu tima |
| Ghost sloj i Stamova konvencija | **Donesena** (D2) | literatura koristi isto |
| Spajanje preko `Integration/` (+ 2 izmjene u test-app) | Preporuka, potvrditi prije M-VIZ (zadnji milestone) | najmanje diranje Renderera |
| Solver se gradi i pokreće potpuno bez Renderera | **Donesena** | `cmake -S Solver ...`, bez OpenGL-a i bez preuzimanja biblioteka |
| Alat za testove: doctest, header zapakovan u `Solver/third_party/doctest/` | **Donesena** (D5) | jedan header, brz; bez `FetchContent`, pa gradnja ne treba internet |
| Optimizacija u ovoj fazi samo sekvencijalna; paralelizacija nije milestone | **Donesena** (D8) | PLAN.md, M4 |

Ostala pitanja (temperatura da/ne, jedinice, granični uslovi na vrhu kutije) vode se kao
D1 do D8 u [PLAN.md](PLAN.md), odjeljak 3 "Odluke" (sve donesene 2026-11-09).

---

## 8. Dim kao objekat u sceni ("nevidljiva kutija")

Dim se simulira unutar **nevidljive kutije**: mreže ćelija sa čvrstim zidovima. Sama kutija se ne
crta, vidi se samo dim u njoj. To omogućava da se dim postavi kao objekat gdje god treba, npr. iznad
dimnjaka u jednoj od scena renderera.

**Podjela odgovornosti:**

| Šta | Ko ga zna |
| :--- | :--- |
| Mreža ćelija, polja, fizika, izvor dima unutar kutije | `Solver` (lokalne koordinate kutije, `[0,1]^3`; `SmokeSource` je relativan) |
| Gdje kutija stoji u svijetu (pozicija, rotacija, veličina u svjetskim jedinicama) | `Integration` / scena (Renderer već ima klasu `Transform`) |
| Crtanje dima unutar kutije | `Integration` (ray marching; kutija određuje gdje zraka ulazi i izlazi) |

**Posljedice:**
- `SmokeSolver` ne zna ništa o sceni. Zato ga je lako testirati bez renderera i ponovo koristiti.
- **Jedna kutija = jedna instanca `SmokeSolver`.** Više dimnjaka u sceni znači više nezavisnih instanci
  (moguće jer nema globalnog stanja, odjeljak 5, pravilo 3).
- **Veličina ćelije u svijetu** je `veličina kutije / broj ćelija`. Solver radi u jedinicama ćelije (odluka D4),
  pa se skaliranje na svjetske jedinice radi samo na strani renderera.
- **Izvor dima** se zadaje relativno unutar kutije. Za dimnjak: kutija je iznad otvora, a izvor je pri dnu kutije
  (otvor dimnjaka).
- **Smjer "gore":** solver koristi lokalni +y kao smjer uzgona. Ako se kutija u svijetu rotira, uzgon i dalje
  djeluje duž njenog lokalnog +y. Zato kutije za dimnjake ostaju uspravne, ili se kasnije dodaje parametar smjera
  uzgona. Provjeriti koja osa je "gore" u Rendereru pri M-VIZ.
- **Visina kutije** mora biti dovoljna da dim ne ostane "odrezan" na vrhu (zatvoren vrh, odluka D3). Visinu bira
  scena. Otvoren vrh je moguća kasnija opcija.
