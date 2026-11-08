#include "vectorization.hpp"
#include "bench.hpp"
// kompajler nece vektorizovati ovaj fajl automatski
namespace vectorization
{
    void saxpy_scalar(const float* x, float* y, std::size_t n, float a)
    {
        PERF_NO_VECTORIZE
        for (std::size_t i = 0; i < n; ++i)
            y[i] = a * x[i] + y[i];
    }

    float dot_scalar(const float* x, const float* y, std::size_t n)
    {
        float sum = 0;
        PERF_NO_VECTORIZE
        for (std::size_t i = 0; i < n; ++i)
            sum += x[i] * y[i];
        return sum;
    }

    void threshold_scalar(const float* x, float* y, std::size_t n, float threshold, float scale)
    {
        PERF_NO_VECTORIZE
        for (std::size_t i = 0; i < n; ++i)
            y[i] = ((x[i] > threshold) ? x[i] * scale : 0.0f);
    }

    void escape_scalar(const float* cx, const float* cy, float* out, std::size_t n, int max_iter)
    {
        PERF_NO_VECTORIZE
        for (std::size_t i = 0; i < n; ++i)
        {
            float x = 0, y = 0;
            int iteration = 0;

            while (x * x + y * y <= 4.0f && iteration < max_iter)
            {
                const float next_x = x * x - y * y + cx[i];
                y = 2.0f * x * y + cy[i];
                x = next_x;
                ++iteration;
            }
            out[i] = static_cast<float>(iteration);
        }
    }
}
