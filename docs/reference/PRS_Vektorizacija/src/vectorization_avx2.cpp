#include "vectorization.hpp"

#if defined(PERFLABS_HAS_AVX2_BINARY)
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace vectorization
{
    void saxpy_avx2(const float* x, float* y, std::size_t n, float a)
    {
#if defined(PERFLABS_HAS_AVX2_BINARY)
        const __m256 scale = _mm256_set1_ps(a);

        std::size_t i = 0;
        for (; i + 8 <= n; i += 8)
        {
            const __m256 xv = _mm256_loadu_ps(x + i);
            const __m256 yv = _mm256_loadu_ps(y + i);
            const __m256 res = _mm256_add_ps(_mm256_mul_ps(scale, xv), yv);
            _mm256_storeu_ps(y + i, res);
        }
        // Osigurati da se uradi za sve elementi, relevatno kada n%8 != 0
        for (; i < n; ++i)
            y[i] = a * x[i] + y[i];

#else
        saxpy_auto(x, y, n, a);
#endif
    }

    void saxpy_avx2_aligned(const float* x, float* y, std::size_t n, float a)
    {
#if defined(PERFLABS_HAS_AVX2_BINARY)
        // x i y moraju biti poravnati na 32 bajta
        const __m256 scale = _mm256_set1_ps(a);

        std::size_t i = 0;
        for (; i + 8 <= n; i += 8)
        {
            const __m256 xv = _mm256_load_ps(x + i);
            const __m256 yv = _mm256_load_ps(y + i);
            const __m256 res = _mm256_add_ps(_mm256_mul_ps(scale, xv), yv);
            _mm256_store_ps(y + i, res);
        }
        // Osigurati da se uradi za sve elementi, relevatno kada n%8 != 0
        for (; i < n; ++i)
            y[i] = a * x[i] + y[i];

#else
        saxpy_auto(x, y, n, a);
#endif
    }

    float dot_avx2(const float* x, const float* y, std::size_t n) 
    {
#if defined(PERFLABS_HAS_AVX2_BINARY)
        __m256 sums = _mm256_setzero_ps();

        std::size_t i = 0;
        for (; i + 8 <= n; i += 8)
        {
            __m256 xv = _mm256_loadu_ps(x + i);
            __m256 yv = _mm256_loadu_ps(y + i);

            __m256 products = _mm256_mul_ps(xv, yv);

            sums = _mm256_add_ps(sums, products);
        }

        alignas(32) float partial[8];

        _mm256_store_ps(partial, sums);

        float total = 0;
        for (float xpart : partial) 
            total += xpart;

        for (; i < n; ++i) 
            total += x[i] * y[i];

        return total;
#else
        return dot_auto(x, y, n);
#endif
    }

    void threshold_avx2(const float* x, float* out, std::size_t n, float t, float a)
    {
#if defined(PERFLABS_HAS_AVX2_BINARY)
        const __m256 threshold = _mm256_set1_ps(t);
        const __m256 scale = _mm256_set1_ps(a);
        const __m256 zero = _mm256_setzero_ps();

        std::size_t i = 0;
        for (; i + 8 <= n; i += 8) 
        {
            const __m256 xv = _mm256_loadu_ps(x + i);

            const __m256 mask = _mm256_cmp_ps(xv, threshold, _CMP_GT_OQ); //maska cuva reultat poredjenja

            const __m256 product = _mm256_mul_ps(xv, scale);

            _mm256_storeu_ps(out + i, _mm256_blendv_ps(zero, product, mask)); //maska kaze da li se uzima proizvod ili 0
        }
        for (; i < n; ++i)
            out[i] = (x[i] > t) ? x[i] * a : 0.0f;
#else
        threshold_auto(x, out, n, t, a);
#endif
    }
}
