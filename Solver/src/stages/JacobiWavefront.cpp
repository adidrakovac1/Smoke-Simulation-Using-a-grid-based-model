#include "stages/ObstacleLists.h"
#include "stages/Stages.h"

#include "Restrict.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace smoke::serial {

// Jacobi iterations with TEMPORAL BLOCKING, as a wavefront in z (decision D19; docs/plan/OPTIMIZATION.md,
// section 3.1, "vremensko blokiranje").
//
// Why. One Jacobi iteration reads pOld and div and writes pNew: about 12 bytes per cell. At 128^3 the three
// fields (~26 MiB) do not fit in the 16 MiB L3 cache of PC-2, so the plain loop streams them from RAM 40 times
// per step and runs at the memory bandwidth limit (PERF_LOG.md, section 8: E = 0.94). The arithmetic units
// mostly wait: CPI 1.4 to 1.6 for jacobiSlabAvx2 at 128^3 against 0.55 at 64^3 (profile of 2026-11-15).
//
// Idea. Iteration t at plane k needs only iteration t-1 at planes k-1, k, k+1. So iteration t can start on
// plane k as soon as iteration t-1 has finished plane k+1; it does not have to wait for the whole grid. A wave
// of D iterations then travels through the grid together, each one a few planes behind the previous one, and
// the planes they share are still in the cache. The grid goes through RAM once per wave instead of once per
// iteration. Every cell is computed by exactly the same row kernel with exactly the same inputs as in the plain
// loop, only at a different time, so the result is bit-identical.
//
// The two buffers. Iteration t writes Y = pressure0 if t is odd and X = pressure if t is even, and reads the
// other one, which is what the plain loop's "swap, then compute" does. Iteration 1 reads nothing: the plain loop
// starts from pressure = 0, so it uses jacobiRowFromZero (JacobiRows.inc). At the end the result is swapped
// into `pressure` when the number of iterations is odd.
//
// Schedule. The planes are grouped in blocks of P planes: block q holds planes qP+1 .. qP+P. A wave of D
// iterations, d = 0..D-1, goes through "ticks" s = 0, 1, 2, .... At tick s, iteration d works on block
// q = s - L*d: it computes the interior of the block's planes, and it FINALIZES some planes: the obstacle cells
// next to the fluid (extendIntoSolids) and the ghost cells (setBoundary, Scalar). All threads take part in
// every tick, each with its own rows j, and meet at a barrier after the tick.
//   * Without obstacles a plane is finalized right after its interior, in the same tick: the x and y ghost
//     cells of a row copy cells of the same row, the ghost plane 0 copies plane 1, and each thread finalizes
//     its own rows. Then L = 2.
//   * With obstacles an obstacle cell takes the mean of its fluid neighbours, which can lie in planes b-1 and
//     b+1 and in rows of other threads. So at block q the planes (q-1)P .. qP-1 are finalized: plane qP was
//     computed in the PREVIOUS tick, behind a barrier. Then L = 2 + ceil(2/P).
//   * Lag L. Iteration d+1 at block c needs iteration d finalized up to plane cP+P+1. At the previous tick
//     iteration d was at block c+L-1 and had finalized up to (c+L-1)P (no obstacles) or (c+L-1)P-1 (with):
//     with the values of L above that is far enough, and behind a barrier.
//   * Reuse of a buffer. Iteration d+1 overwrites the buffer of iteration d-1. The last readers of iteration
//     d-1 at block c are iteration d at planes cP .. cP+P+1, which iteration d finished L-1 >= 1 ticks earlier.
//   * Within one tick no two threads touch the same cell, and a thread never reads a cell another thread
//     writes in the same tick (it reads planes finished in earlier ticks).
// The ghost planes 0 and nz+1 are part of finalizing planes 1 and nz. A wave ends with a barrier, so the next
// wave starts from a finished iterate. With one thread the barriers do nothing.
//
// The two knobs. D = iterations per wave: the grid goes through RAM once per wave instead of once per
// iteration, but the wave's planes (about L*D*P of each field) must stay in the cache. P = planes per tick: the
// barrier costs ~1 us, so a tick must carry enough work (P = nz with D = 1 is the plain loop with one barrier
// per iteration). chooseShape picks both from the grid and the team size; PERF_LOG.md, P-004 has the sweep.
namespace {

// dst[i] = src[i], i in [0, n): one row of a ghost face (Scalar rule: plain copy).
inline void copyRow(float* SMOKE_RESTRICT dst, const float* SMOKE_RESTRICT src, int n) {
#pragma omp simd
    for (int i = 0; i < n; ++i) dst[i] = src[i];
}

struct Geometry {
    int nx, ny, nz;
    std::ptrdiff_t sJ, sK;
    int j0, j1;          // this thread's interior rows [j0, j1)
    bool ownsFirstRow;   // j = 1 is ours: we also write ghost row j = 0
    bool ownsLastRow;    // j = ny is ours: we also write ghost row j = ny + 1
};

// extendIntoSolids (Obstacles.cpp) restricted to the boundary solid cells of rows [j0, j1) of plane b. The list
// is sorted by cell index (buildObstacleLists walks k, j, i in increasing order), so those cells are one
// contiguous part of it, found by binary search. Same arithmetic in the same order as extendIntoSolids.
void extendRows(float* data, const ObstacleLists& o, const Geometry& g, int b) {
    const std::uint32_t lo = static_cast<std::uint32_t>(b * g.sK + g.j0 * g.sJ);
    const std::uint32_t hi = static_cast<std::uint32_t>(b * g.sK + g.j1 * g.sJ);
    const auto first = std::lower_bound(o.boundarySolid.begin(), o.boundarySolid.end(), lo);
    for (auto it = first; it != o.boundarySolid.end() && *it < hi; ++it) {
        const std::size_t bIndex = static_cast<std::size_t>(it - o.boundarySolid.begin());
        float sum = 0.0f;
        for (std::uint32_t n = o.neighbourStart[bIndex]; n < o.neighbourStart[bIndex + 1]; ++n)
            sum += data[o.neighbour[n]];
        data[*it] = sum * o.neighbourWeight[bIndex];
    }
}

// Ghost cells of rows [j0, j1) of plane b, by the three passes of setBoundary (Scalar) in the same order.
void finalizeRows(float* data, const ObstacleLists* obstacles, const Geometry& g, int b) {
    if (g.j0 >= g.j1) return;
    if (obstacles) extendRows(data, *obstacles, g, b);
    float* const plane = data + b * g.sK;
    for (int j = g.j0; j < g.j1; ++j) {  // pass 1: x walls
        float* const row = plane + j * g.sJ;
        row[0]        = row[1];
        row[g.nx + 1] = row[g.nx];
    }
    const int rowLength = g.nx + 2;
    if (g.ownsFirstRow) copyRow(plane, plane + g.sJ, rowLength);                               // pass 2: y walls
    if (g.ownsLastRow) copyRow(plane + (g.ny + 1) * g.sJ, plane + g.ny * g.sJ, rowLength);
    const int rowLo = g.ownsFirstRow ? 0 : g.j0;                                                // pass 3: z walls
    const int rowHi = g.ownsLastRow ? g.ny + 2 : g.j1;
    if (b == 1)
        for (int j = rowLo; j < rowHi; ++j) copyRow(data + j * g.sJ, plane + j * g.sJ, rowLength);
    if (b == g.nz)
        for (int j = rowLo; j < rowHi; ++j) copyRow(data + (g.nz + 1) * g.sK + j * g.sJ, plane + j * g.sJ, rowLength);
}

struct Shape {
    int depth;   // D: iterations per wave
    int planes;  // P: planes per tick
};

// TODO(measure): the constants are the best values of the sweep on PC-2 (PERF_LOG.md, P-004).
Shape chooseShape(const GridSize& s, std::ptrdiff_t sK, int threads, int waveDepth, int planesPerTick) {
    const std::size_t fieldBytes = static_cast<std::size_t>(sK) * sizeof(float) * static_cast<std::size_t>(s.nz + 2);
    (void)threads;  // the same shapes measured best for 1 and for 6 threads
    Shape shape{20, 1};
    if (3 * fieldBytes <= (8u << 20)) {
        // The three fields fit in the L3 cache together (64^3: 3.6 MiB): there is no RAM traffic to save, the
        // limit is the L3 bandwidth. Short waves with 4 planes per tick keep the ticks long enough for the
        // barrier (sweep at 64^3, 6 threads: D=4,P=4 0.034 ms per iteration, plain loop 0.042 ms).
        shape = Shape{4, 4};
    }
    // else: long waves, one plane per tick (sweep at 128^3, 6 threads: D=20,P=1 0.28 ms, D=8,P=2 0.34 ms,
    // plain loop 0.92 ms; 1 thread: D=20 0.57 ms, plain loop 1.20 ms).
    if (waveDepth > 0) shape.depth = waveDepth;
    if (planesPerTick > 0) shape.planes = planesPerTick;
    shape.planes = std::max(1, std::min(shape.planes, s.nz));
    return shape;
}

} // namespace

void jacobiWavefront(Field3D& pressure, Field3D& pressure0, const Field3D& div, int iterations, int waveDepth,
                     const ObstacleLists* obstacles, const Team& team, int planesPerTick) {
    const GridSize s = pressure.size();
    if (iterations <= 0) {
        // The plain loop leaves pressure = 0 and pressure0 untouched.
        if (team.tid == 0) pressure.fill(0.0f);
        team.sync();
        return;
    }

    const Shape shape = chooseShape(s, pressure.strideK(), team.size, waveDepth, planesPerTick);
    const int P = shape.planes;
    const int L = obstacles ? 2 + (2 + P - 1) / P : 2;
    const int blocks = (s.nz + P - 1) / P;                          // interior blocks 0 .. blocks-1
    const int lastBlock = obstacles ? (s.nz + 1 + P - 1) / P : blocks - 1;  // the trailing finalize, see above
    const int depthLimit = shape.depth;

    Geometry g{};
    g.nx = s.nx;
    g.ny = s.ny;
    g.nz = s.nz;
    g.sJ = pressure.strideJ();
    g.sK = pressure.strideK();
    g.j0 = 1 + static_cast<int>((static_cast<long long>(team.tid) * s.ny) / team.size);
    g.j1 = 1 + static_cast<int>((static_cast<long long>(team.tid + 1) * s.ny) / team.size);
    g.ownsFirstRow = g.j0 <= 1 && 1 < g.j1;
    g.ownsLastRow  = g.j0 <= s.ny && s.ny < g.j1;

    const JacobiPlaneRowsFn rows = jacobiPlaneRowsKernel();
    float* const X = pressure.data();     // written by even iterations
    float* const Y = pressure0.data();    // written by odd iterations
    const float* const d = div.data();

    for (int done = 0; done < iterations;) {
        const int D = std::min(depthLimit, iterations - done);
        const int ticks = lastBlock + 1 + L * (D - 1);
        for (int tick = 0; tick < ticks; ++tick) {
            for (int w = 0; w < D; ++w) {
                const int q = tick - L * w;  // this iteration's block in this tick
                if (q < 0 || q > lastBlock) continue;
                const int t = done + w + 1;  // 1-based iteration number
                float* const out = (t % 2 == 1) ? Y : X;
                const float* const in = (t % 2 == 1) ? X : Y;
                if (q < blocks) {
                    const int a0 = q * P + 1, a1 = std::min(q * P + P, s.nz);
                    for (int a = a0; a <= a1; ++a) {
                        rows(out, in, d, g.nx, g.j0, g.j1, a, g.sJ, g.sK, t == 1);
                        if (!obstacles) finalizeRows(out, nullptr, g, a);
                    }
                }
                if (obstacles) {
                    const int b0 = std::max(1, (q - 1) * P), b1 = std::min(q * P - 1, s.nz);
                    for (int b = b0; b <= b1; ++b) finalizeRows(out, obstacles, g, b);
                }
            }
            team.sync();
        }
        done += D;
    }

    // The plain loop ends with the last iterate in `pressure` and the one before it in `pressure0`.
    // After one iteration, pressure0 is the starting p = 0 (X was never written here).
    if (team.tid == 0) {
        if (iterations == 1) pressure.fill(0.0f);
        if (iterations % 2 == 1) pressure.swap(pressure0);
    }
    team.sync();
}

} // namespace smoke::serial
