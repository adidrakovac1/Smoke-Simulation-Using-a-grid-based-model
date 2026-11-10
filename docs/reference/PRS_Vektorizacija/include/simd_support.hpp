#pragma once

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace simd
{
    class AlignedFloats
    {
    private:
        float* _data = nullptr;

    public:
        explicit AlignedFloats(std::size_t count)
        {
            const std::size_t bytes = count * sizeof(float);
#if defined(_MSC_VER)
            _data = static_cast<float*>(_aligned_malloc(bytes, 32));
#else
            void* ptr = nullptr;
            if (posix_memalign(&ptr, 32, bytes) != 0)
                ptr = nullptr;
            _data = static_cast<float*>(ptr);
#endif
            if (!_data)
                throw std::bad_alloc();
        }

        ~AlignedFloats()
        {
#if defined(_MSC_VER)
            _aligned_free(_data);
#else
            std::free(_data);
#endif
        }

        AlignedFloats(const AlignedFloats&) = delete;
        AlignedFloats& operator=(const AlignedFloats&) = delete;

        float* data() const
        {
            return _data;
        }
    };

    inline void check_vector(const std::vector<float>& expected, const float* actual,
                             std::size_t n, float absolute_tolerance = 1e-4f)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!std::isfinite(actual[i]) ||  std::fabs(actual[i] - expected[i]) > absolute_tolerance * (1.0f + std::fabs(expected[i])))
                throw std::runtime_error("SIMD-not corrent index: " + std::to_string(i));
        }
    }
}
