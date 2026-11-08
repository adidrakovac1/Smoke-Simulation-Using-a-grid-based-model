#include "particle_update.hpp"
#include "bench.hpp"

#if defined(PERFLABS_HAS_AVX2_BINARY)
#include <immintrin.h>
#endif
#include <cmath>
namespace particle 
{
    void step_soa_avx2(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit)
    {
#if defined(PERFLABS_HAS_AVX2_BINARY)
        const __m256 DT = _mm256_set1_ps(dt);
        const __m256 D = _mm256_set1_ps(damping);
        const __m256 B = _mm256_set1_ps(bounce);
        const __m256 HIGH = _mm256_set1_ps(limit);
        const __m256 LOW = _mm256_set1_ps(-limit);
        const __m256 SIGN = _mm256_set1_ps(-0.0f);

        std::size_t i = 0;
        for (; i + 8 <= n; i += 8)
        {
            const __m256 a = _mm256_loadu_ps(acceleration + i);

            const __m256 old_v = _mm256_loadu_ps(vx + i);

            const __m256 old_x = _mm256_loadu_ps(x + i);

            const __m256 v = _mm256_mul_ps(_mm256_add_ps(old_v, _mm256_mul_ps(a, DT)), D);

            const __m256 next_x_possible = _mm256_add_ps(old_x, _mm256_mul_ps(v, DT));

            const __m256 above = _mm256_cmp_ps(next_x_possible, HIGH, _CMP_GT_OQ);

            const __m256 below = _mm256_cmp_ps(next_x_possible, LOW, _CMP_LT_OQ);

            const __m256 abs_v = _mm256_mul_ps(_mm256_andnot_ps(SIGN, v), B);

            const __m256 up = _mm256_sub_ps(_mm256_setzero_ps(), abs_v);

            const __m256 down = _mm256_blendv_ps(v, abs_v, below);

            const __m256 next_v = _mm256_blendv_ps(down, up, above);

            const __m256 next_x = _mm256_min_ps(HIGH, _mm256_max_ps(LOW, next_x_possible));

            _mm256_storeu_ps(vx + i, next_v);

            _mm256_storeu_ps(x + i, next_x);
        }
        for (; i < n; ++i)
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
#else
        step_soa_branchless(acceleration, x, vx, n, dt, damping, bounce, limit);
#endif
    }
}
