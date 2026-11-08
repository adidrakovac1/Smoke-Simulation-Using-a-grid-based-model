#pragma once

#include "bench.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace memory
{
    using u32 = std::uint32_t;

    //std::vector cuva podatke u row-major
    inline std::uint64_t traverse(const std::vector<u32>& a, std::size_t n, bool row_major)
    {
        std::uint64_t sum = 0;
        if (row_major)
        {
            for (std::size_t r = 0; r < n; ++r)
                for (std::size_t c = 0; c < n; ++c)
                    sum += a[r * n + c];
        }
        else
        {
            for (std::size_t c = 0; c < n; ++c)
                for (std::size_t r = 0; r < n; ++r)
                    sum += a[r * n + c];
        }
        return sum;
    }


    inline void matmul(const float* a, const float* b, float* c, std::size_t n, int order)
    {
        std::fill(c, c + n * n, 0.0f);

        switch (order)
        {
        case 0: // i-j-k
            // A: sekvencijalni pristup elementima reda (dobra lokalnost)
            // B: pristup elementima kolone, korak n (losa lokalnost)
            // C: ponovljeni pristup istom elementu (odlicna lokalnost)
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j)
                    for (std::size_t k = 0; k < n; ++k)
                        c[i * n + j] += a[i * n + k] * b[k * n + j];
            break;

        case 1: // i-k-j
            // A: ponovljeni pristup istom elementu (odlicna lokalnost)
            // B: sekvencijalni pristup elementima reda (dobra lokalnost)
            // C: sekvencijalni pristup elementima reda (dobra lokalnost)
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t k = 0; k < n; ++k)
                    for (std::size_t j = 0; j < n; ++j)
                        c[i * n + j] += a[i * n + k] * b[k * n + j];
            break;

        case 2: // j-i-k
            // A: sekvencijalni pristup elementima reda (dobra lokalnost)
            // B: pristup elementima kolone, korak n (losa lokalnost)
            // C: ponovljeni pristup istom elementu (odlicna lokalnost)
            for (std::size_t j = 0; j < n; ++j)
                for (std::size_t i = 0; i < n; ++i)
                    for (std::size_t k = 0; k < n; ++k)
                        c[i * n + j] += a[i * n + k] * b[k * n + j];
            break;

        case 3: // j-k-i
            // A: pristup elementima kolone, korak n (losa lokalnost)
            // B: ponovljeni pristup istom elementu (odlicna lokalnost)
            // C: pristup elementima kolone, korak n (losa lokalnost)
            for (std::size_t j = 0; j < n; ++j)
                for (std::size_t k = 0; k < n; ++k)
                    for (std::size_t i = 0; i < n; ++i)
                        c[i * n + j] += a[i * n + k] * b[k * n + j];
            break;

        case 4: // k-i-j
            // A: ponovljeni pristup istom elementu (odlicna lokalnost)
            // B: sekvencijalni pristup elementima reda (dobra lokalnost)
            // C: sekvencijalni pristup elementima reda (dobra lokalnost)
            for (std::size_t k = 0; k < n; ++k)
                for (std::size_t i = 0; i < n; ++i)
                    for (std::size_t j = 0; j < n; ++j)
                        c[i * n + j] += a[i * n + k] * b[k * n + j];
            break;

        case 5: // k-j-i
            // A: pristup elementima kolone, korak n (losa lokalnost)
            // B: ponovljeni pristup istom elementu (odlicna lokalnost)
            // C: pristup elementima kolone, korak n (losa lokalnost)
            for (std::size_t k = 0; k < n; ++k)
                for (std::size_t j = 0; j < n; ++j)
                    for (std::size_t i = 0; i < n; ++i)
                        c[i * n + j] += a[i * n + k] * b[k * n + j];
            break;
        default:
            throw std::invalid_argument("Bad order");
        }

    }

    inline void matmul_tiled(const float* a, const float* b, float* c ,std::size_t n, std::size_t block)
    {

        //i-k-j raspored
        std::fill(c, c + n * n, 0.0f);

        for (std::size_t ii = 0; ii < n; ii += block)
            for (std::size_t kk = 0; kk < n; kk += block)
                for (std::size_t jj = 0; jj < n; jj += block)
                    for (std::size_t i = ii; i < std::min(ii + block, n); ++i)
                        for (std::size_t k = kk; k < std::min(kk + block, n); ++k)
                            for (std::size_t j = jj; j < std::min(jj + block, n); ++j)
                                c[i * n + j] += a[i * n + k] * b[k * n + j];
    }

}
