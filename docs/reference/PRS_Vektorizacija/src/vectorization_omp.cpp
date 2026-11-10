#include "vectorization.hpp"


namespace vectorization
{
    void saxpy_omp(const float* x, float* y, std::size_t n, float a) 
    {
#if defined(PERFLABS_HAS_OPENMP)
#pragma omp simd
#endif
        for (std::size_t i = 0; i < n; ++i)
            y[i] = a * x[i] + y[i];
    }


    float dot_omp(const float* x, const float* y, std::size_t n) 
    {
        float sum = 0;
#if defined(PERFLABS_HAS_OPENMP)
#if !defined(_MSC_VER)
#pragma omp simd reduction(+:sum)
#endif
#endif
        for (std::size_t i = 0; i < n; ++i)
            sum += x[i] * y[i];
        return sum;
    }
    void threshold_omp(const float* x, float* y, std::size_t n, float t, float scale)
    {
#if defined(PERFLABS_HAS_OPENMP)
#pragma omp simd
#endif
        for (std::size_t i = 0; i < n; ++i)
            y[i] = (x[i] > t) ? x[i] * scale : 0.0f;
    }
}
