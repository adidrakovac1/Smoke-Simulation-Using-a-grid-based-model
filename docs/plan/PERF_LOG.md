# PERF_LOG: evidencija optimizacija i mjerenja

Pravila vođenja su u [OPTIMIZATION.md](OPTIMIZATION.md). Ukratko: **jedan unos po izmjeni, i
kad ne pomaže**; mjerenje u istoj sesiji sa osnovom; glavne metrike su ubrzanje **S** i delta **Δ**.
Strojno čitljiva kopija je `perf/results.csv`. Oba fajla se mijenjaju u istom commitu kao i kod.

Trenutno nema unosa (serijska faza još nije izmjerena). Prvi unosi su osnove **B0** i **B1**.

---

## 1. Otisci računara tima

Popuniti jednom po računaru, a promijeniti kad se promijeni nešto od ovoga (drajver, kompajler,
plan napajanja).

| Oznaka | Vlasnik | Procesor | Jezgara/niti | L2 / L3 | RAM | AVX2 | Kompajler | Napomena |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| PC-1 | | | | | | | g++ 16.2.0 (MSYS2 UCRT64) | |
| PC-2 | | | | | | | | |
| PC-3 | | | | | | | | |

Komanda za procesor (PowerShell):
`Get-CimInstance Win32_Processor | Select Name,NumberOfCores,NumberOfLogicalProcessors,L2CacheSize,L3CacheSize`

---

## 2. Šablon unosa (kopirati za svaku izmjenu)

```
### P-XXX: <kratak naslov>

- Datum / autor / računar: <YYYY-MM-DD> / <ime> / <PC-n>
- Commit: <kratki hash> (osnova: <kratki hash>)
- Nivo i tehnika: <nivo 1, T8 omp simd | nivo 2, OpenMP niti | ...>
- Jezgro / faza: <npr. Projection::jacobi>
- Hipoteza: <šta očekujem i zašto>
- Parametri: mreža <64^3>, Jacobi iteracija <40>, niti <1>, koraka <100 + 10 zagrijavanja>
- Gradnja: <Release>, oznake <-O2 ...>, kompajler <g++ 16.2.0>
- Uslovi: <priključen, visoke performanse, drugi programi zatvoreni>

| Varijanta | Medijan (ms) | Min (ms) | Raspon (ms) | GB/s | E |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Osnova | | | | | |
| Nova | | | | | |

- Ubrzanje S = T_osnova / T_nova =
- Delta Δ = (T_osnova - T_nova) / T_osnova =
- Tačnost: najveća razlika prema referentnom rezultatu = <vrijednost>, testovi <prolaze / ne prolaze>
- Zaključak: <ZADRŽATI | ODBACITI | JOŠ MJERITI>
- Šta smo naučili: <jedna do dvije rečenice, i kad nije pomoglo>
```

---

## 3. Unosi

| ID | Datum | Računar | Nivo / tehnika | Jezgro | Mreža | Niti | S | Δ | Zaključak |
| :--- | :--- | :--- | :--- | :--- | :--- | ---: | ---: | ---: | :--- |
| | | | | | | | | | |

(Ispod tabele idu puni unosi po šablonu, najnoviji na kraju.)

---

## 4. Šema `perf/results.csv`

Jedna linija po mjerenom jezgru i varijanti u jednoj sesiji. Zaglavlje:

```
entry_id,date,machine,commit,base_commit,level,technique,stage,grid_nx,grid_ny,grid_nz,jacobi_iters,threads,steps,build_type,compiler,flags,variant,median_ms,min_ms,max_ms,gbps,efficiency_E,speedup_S,delta_pct,max_abs_diff,verdict
```

- `variant`: npr. `B0`, `B1`, `restrict`, `omp_simd`, `avx2`.
- `speedup_S` i `delta_pct` se računaju prema osnovi **iz iste sesije**.
- `max_abs_diff`: najveća razlika prema referentnom rezultatu (VERIFICATION.md, odjeljak 3).
- `verdict`: `keep`, `drop` ili `more`.
- Fajl se samo dopunjava; stari redovi se ne brišu.
