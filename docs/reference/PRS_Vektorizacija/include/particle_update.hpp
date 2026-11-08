#pragma once
#include <cstddef>
#include <vector>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>
namespace particle
{
    struct Particle
    {
        float x, y, z;
        float vx, vy, vz;
        float acceleration;
        float mass;
    };

    struct SoA
    {
        std::vector<float> x, vx, acc;
    };

    inline SoA make_soa(const std::vector<Particle>& src)
    {
        SoA dst;
        dst.x.reserve(src.size()); dst.vx.reserve(src.size()); dst.acc.reserve(src.size());

        for (const auto& a : src)
        {
            dst.x.push_back(a.x);
            dst.vx.push_back(a.vx);
            dst.acc.push_back(a.acceleration);
        }
        return dst;
    }

    inline void verify(const std::vector<Particle>& ref, const SoA& actual)
    {
        for (std::size_t i = 0; i < ref.size(); ++i)
        {
            const auto near = [](float a, float b)
                {
                    return std::fabs(a - b) <= 0.0001f * (1.f + std::fabs(a));
                };

            if (!near(ref[i].x, actual.x[i]) || !near(ref[i].vx, actual.vx[i]))
                throw std::runtime_error("Error-index " + std::to_string(i));
        }
    }



    void step_aos_scalar(Particle* p, std::size_t n, float dt, float damping, float bounce, float limit);

    void step_soa_scalar(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit);

    void step_soa_branch(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit);

    void step_soa_branchless(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit);

    void step_soa_avx2(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit);

    void step_soa_simd(const float* acceleration, float* x, float* vx, std::size_t n, float dt, float damping, float bounce, float limit);

}
