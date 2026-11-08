# Integration

**Zasad prazno.** Ovdje se u zadnjem milestone-u (**M-VIZ**, vizualizacija) spajaju `Solver/` i `Renderer/`.
Ovo je jedino mjesto koje smije uključivati oba (pravilo 3 u `../docs/plan/ARCHITECTURE.md`).

Planirani sadržaj:

| Dio | Šta radi |
| :--- | :--- |
| `SmokeScene` | `Scene` iz Renderera koja drži jedan `SmokeSolver` i transformaciju "nevidljive kutije" (npr. iznad dimnjaka) |
| `Texture3D` | OpenGL 3D tekstura za gustoću (Renderer je nema) |
| `VolumeRaymarchPass` | ray marching kroz kutiju, crtanje u HDR bafer u `OnPostRender` |
| `SmokeUI` | ImGui meni: veličina mreže (32/64/96/128 + Apply), parametri, pauza, korak, reset |

Detalji: `../docs/plan/ARCHITECTURE.md` (odjeljci 6 i 8) i `../docs/plan/PLAN.md` (M-VIZ).
Ništa se ovdje ne piše dok Solver nije gotov, optimiziran i izmjeren.
