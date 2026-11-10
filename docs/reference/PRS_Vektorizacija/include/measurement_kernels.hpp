#pragma once

#include "bench.hpp"

#include <cstddef>
#include <cstdint>

namespace measurement
{
    // kompajler moze citav poziv izbaciti
    PERF_NOINLINE inline void unobserved_work(std::uint64_t seed, std::size_t steps)
    {
        for (std::size_t i = 0; i < steps; ++i)
        {
            seed ^= seed << 10;
            seed ^= seed >> 5;
            seed ^= seed << 15;
        }
    }

    inline std::uint64_t fn(std::uint64_t x)
    {
        x ^= x << 10;
        x ^= x >> 5;
        x ^= x << 15;
        return x;
    }
    //nova iteracija zavisi od prethodne
    PERF_NOINLINE inline std::uint64_t dependent(std::uint64_t seed, std::size_t operations)
    {
        for (std::size_t i = 0; i < operations; ++i)
            seed = fn(seed); 
        return seed;
    }

    //4 nezavisne iteracije
    PERF_NOINLINE inline std::uint64_t independent(std::uint64_t seed, std::size_t steps)
    {
        std::uint64_t a = seed, b = seed + 1, c = seed + 2, d = seed + 3;

        for (std::size_t i = 0; i < steps; ++i)
        {
            a = fn(a);
            b = fn(b);
            c = fn(c);
            d = fn(d);
        }
        return a ^ b ^ c ^ d; 
    }
}
