// M1 demo: a sphere of density carried by a constant velocity field, saved as PGM images
// (docs/plan/PLAN.md, M1 criterion; docs/plan/IMPLEMENTATION_PLAN.md, sections 4.5 and 8, P1).
// It lives in the test library because it sets the initial state directly through the internal stages;
// the public SmokeSolver interface stays unchanged. Started with:  smoke_cli --demo advect ...

#include "PgmWriter.h"
#include "selftest.h"
#include "stages/Stages.h"
#include "test_helpers.h"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace smoke_test {

int runAdvectionDemo(const std::string& folder, int every, std::ostream& out) {
    using namespace smoke::serial;

    // Same scenario as test T1 (tests/test_advection.cpp): choices of the demo, not physical constants.
    const smoke::GridSize g{64, 64, 64};
    const float cx = 20.0f, cy = 32.5f, cz = 32.5f, radius = 8.0f;
    const float c = 30.0f, dt = 1.0f / 60.0f;
    const int   steps = 40;

    smoke::SmokeState s;
    s.resize(g);
    fillSphere(s.density, cx, cy, cz, radius, 1.0f);
    setBoundary(s.density, BoundaryKind::Scalar);
    fillInterior(s.u, c);
    setVelocityBoundary(s);

    const bool dump = !folder.empty();
    if (dump) std::filesystem::create_directories(folder);

    // View of the interior of the density field, the same layout the solver hands to the renderer.
    auto view = [&]() {
        smoke::DensityView v;
        v.nx = g.nx; v.ny = g.ny; v.nz = g.nz;
        v.strideJ = s.density.strideJ();
        v.strideK = s.density.strideK();
        v.origin  = &s.density.at(1, 1, 1);
        return v;
    };
    const int slice = g.nz / 2;  // 0-based interior index 32 = cell k = 33, next to the sphere centre (32.5)
    // Fixed grey scale: 1.0 is the initial density, so any loss of peak density shows as a darker image.
    auto save = [&](int step) {
        if (!dump) return true;
        std::ostringstream name;
        name << folder << "/advect_" << std::setw(4) << std::setfill('0') << step << ".pgm";
        return smoke_io::writePgmSliceXY(name.str(), view(), slice, 1.0f);
    };

    out << "=== Demo: advekcija kugle gustoce (M1) ===\n"
        << "Mreza 64x64x64, kugla r = 8 celija u (20, 32.5, 32.5), brzina u = (30, 0, 0) celija/s, dt = 1/60 s.\n"
        << "Ocekivano: teziste se pomjera za c*dt = 0.5 celije po koraku, oblik ostaje okrugao (blago razmazan).\n\n"
        << "  korak   teziste x   ocekivano   masa        max gustoca\n";

    const double x0 = centroidX(s.density);
    bool ok = save(0);
    for (int step = 0; step <= steps; ++step) {
        if (step > 0) advectScalars(s, dt, wholeInterior(g));
        if (step % std::max(1, every) == 0 || step == steps) {
            if (step > 0) ok = save(step) && ok;
            out << "  " << std::setw(5) << step << std::fixed << std::setprecision(4) << std::setw(12)
                << centroidX(s.density) << std::setw(12) << x0 + step * double(c * dt) << std::setw(12)
                << totalMass(s.density) << std::setw(12) << maxInterior(s.density) << "\n";
        }
    }

    const long long bad = countNonFinite(s.density);
    out << "\nNaN/Inf celija: " << bad << "\n";
    if (dump) out << "Slike presjeka (x-y, sloj k = " << slice + 1 << "): " << folder << (ok ? "" : "  (GRESKA pri pisanju)") << "\n";
    out << "Status: " << ((bad == 0 && ok) ? "OK" : "GRESKA") << "\n";
    return (bad == 0 && ok) ? 0 : 1;
}

} // namespace smoke_test
