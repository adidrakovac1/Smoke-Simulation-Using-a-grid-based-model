#include "bench.hpp"
#include "particle_update.hpp"
#include <cmath>


namespace particle
{
    void step_soa_simd(const float* PERF_RESTRICT acceleration, float* PERF_RESTRICT x, float* PERF_RESTRICT vx, std::size_t n, float dt, float damping, float bounce, float limit)
    {
        const float neg_limit = -limit;  

#if defined(PERFLABS_HAS_OPENMP)
#pragma omp simd
#endif
        for (std::size_t i = 0; i < n; ++i)
        {
            const float v = (vx[i] + acceleration[i] * dt) * damping;
            const float new_x = x[i] + v * dt;
            const float abs_v = std::fabs(v) * bounce;

            float out_v = v;
            out_v = (new_x > limit) ? -abs_v : out_v;
            out_v = (new_x < neg_limit) ? abs_v : out_v;

            float out_x = new_x;
            out_x = (out_x > limit) ? limit : out_x;
            out_x = (out_x < neg_limit) ? neg_limit : out_x;

            vx[i] = out_v;
            x[i] = out_x;
        }
    }
}

