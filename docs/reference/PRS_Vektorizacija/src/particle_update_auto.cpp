#include "particle_update.hpp"
#include "bench.hpp"
#include <algorithm>
#include <cmath>
namespace particle 
{
    void step_soa_branch(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            float v = (vx[i] + acceleration[i] * dt) * damping;

            float position = x[i] + v * dt;

            if (position > limit)
            {
                position = limit;
                v = -std::fabs(v) * bounce;
            }
            else if (position < -limit)
            {
                position = -limit;
                v = std::fabs(v) * bounce;
            }

            vx[i] = v;
            x[i] = position;
        }
    }
    void step_soa_branchless(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit)
    {
        // Iako su if/else i ternarni operator funkcionalno jednaki, ne moraju rezultirati istim masinskim instrukcijama
        //kompajler moze ternarni operator realizovati bez grananja
        for (std::size_t i = 0; i < n; ++i)
        {
            const float v = (vx[i] + acceleration[i] * dt) * damping;

            const float new_x = x[i] + v * dt;

            const float abs_v = std::fabs(v) * bounce;

            vx[i] = (new_x > limit) ? -abs_v : ((new_x < -limit) ? abs_v : v);

            x[i] = std::min(limit, std::max(-limit, new_x));
        }
    }
}
