#include "particle_update.hpp"
#include "bench.hpp"
#include <cmath>

namespace particle
{
    void step_aos_scalar(Particle* p, std::size_t n, float dt, float damping, float bounce, float limit)
    {
        PERF_NO_VECTORIZE
        for (std::size_t i = 0; i < n; ++i) 
        {
            Particle& a = p[i];

            float velocity = (a.vx + a.acceleration * dt) * damping;

            float position = a.x + velocity * dt;

            if (position > limit) 
            { 
                position = limit; 
                velocity = -std::fabs(velocity) * bounce;
            }
            else if (position < -limit) 
            { 
                position = -limit;
                velocity = std::fabs(velocity) * bounce; 
            }

            a.vx = velocity;
            a.x = position;
        }
    }

    void step_soa_scalar(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit)
    {
        PERF_NO_VECTORIZE
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


}
