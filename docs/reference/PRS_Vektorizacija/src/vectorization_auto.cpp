#include "vectorization.hpp"
#include "bench.hpp"

namespace vectorization
{
    void saxpy_auto(const float* x, float* y, std::size_t n, float a)
    {
        for (std::size_t i = 0; i < n; ++i)
            y[i] = a * x[i] + y[i];
    }

    float dot_auto(const float* x, const float* y, std::size_t n)
    {
        // kompajleri nece vektorizirati ovo, treba flag za dozvolu promjenu redoslijeda operacija nad ieee754 brojevima, -ffast-math
        float sum = 0;
        for (std::size_t i = 0; i < n; ++i)
            sum += x[i] * y[i];
        return sum;
    }

    void threshold_auto(const float* x, float* y, std::size_t n, float threshold, float scale)
    {
        // SIMD maske umjesto grananja
        for (std::size_t i = 0; i < n; ++i)
            y[i] = ((x[i] > threshold) ? x[i] * scale : 0.0f);
    }

    void escape_auto(const float* cx, const float* cy, float* out, std::size_t n, int max_iter)
    {
        // razlicit broj iteracija za pojedine SIMD elemente komplikuje vektorizaciju
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


    void fn_may_alias(const float* a, const float* b, float* out, std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
            out[i] = a[i] + 2.0f * b[i];
    }
    void fn_no_alias(const float* PERF_RESTRICT a, const float* PERF_RESTRICT b, float* PERF_RESTRICT out, std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
            out[i] = a[i] + 2.0f * b[i];
    }


    // zavisnosti medju iteracijama, ne moze vektorizacija sa ovim algoritmom
    void prefix_scan(const unsigned* in, unsigned* out, std::size_t n)
    {
        if (n == 0)
            return;

        out[0] = in[0];

        for (std::size_t i = 1; i < n; ++i)
            out[i] = out[i - 1] + in[i];
    }

}
