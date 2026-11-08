#pragma once

#include "smoke/Field3D.h"
#include "smoke/GridSize.h"

#include <array>

namespace smoke {

/// All simulation fields. Internal to the Solver (not part of the public interface).
///
/// "*0" fields are the second buffer of each pair: advection and Jacobi READ from the old field
/// and WRITE into the new one, so no cell depends on the processing order (this is what later makes
/// the stages safe to parallelise). Swapping the two is a pointer swap, not a copy.
struct SmokeState {
    Field3D density,     density0;
    Field3D temperature, temperature0;
    Field3D u, u0;
    Field3D v, v0;
    Field3D w, w0;
    Field3D pressure,    pressure0;
    Field3D divergence;

    std::array<Field3D*, 13> fields() {
        return {&density, &density0, &temperature, &temperature0, &u, &u0, &v, &v0,
                &w, &w0, &pressure, &pressure0, &divergence};
    }

    void resize(const GridSize& s) {
        for (Field3D* f : fields()) f->resize(s);
    }
    void clear() {
        for (Field3D* f : fields()) f->fill(0.0f);
    }
};

} // namespace smoke
