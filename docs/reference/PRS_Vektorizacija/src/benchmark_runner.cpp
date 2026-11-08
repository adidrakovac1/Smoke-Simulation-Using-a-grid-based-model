#include "benchmark_runner.hpp"
#include "measurement_kernels.hpp"
#include "memory_kernels.hpp"
#include "simd_support.hpp"
#include "vectorization.hpp"
#include "bench.hpp"
#include "particle_update.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <array>

namespace perf
{
    BenchmarkRunner::BenchmarkRunner(const Config& config)
        : _config(config), _report(config)
    {
    }

    void BenchmarkRunner::run()
    {
        run_measurement();

        run_memory();

        run_vectorization();

        run_particle_update();

        _report.print_summary();
    }

    void BenchmarkRunner::run_measurement()
    {
        using namespace measurement;
        const Config& cfg = _config;
        Report& r = _report;

        const std::size_t operations = std::max<std::size_t>(2'000'000, cfg.n * 8);
        const std::uint64_t seed = 0x123456789abcdefULL ^ cfg.n;
        std::uint64_t saved = seed;

        const auto discarded = measure(cfg,
            [&] {
                unobserved_work(seed, operations);
            },
            [&] {
                return static_cast<double>(saved);
            });

        r.add("Odbaceni rezultat", discarded, discarded.median_s * 1e9 / operations, "ns/iteration");

        const auto retained = measure(cfg,
            [&] {
                saved = dependent(seed, operations);
            },
            [&] {
                return static_cast<double>(saved);
            });
        r.add("Zadrzani rezultat (zavisnost medju iteracijama)", retained, retained.median_s * 1e9 / operations, "ns/operation");

        const auto retained_4 = measure(cfg,
            [&] {
                saved = independent(seed, operations / 4);
            },
            [&] {
                return static_cast<double>(saved);
            });
        r.add("4 nezavisne:", retained_4, retained_4.median_s * 1e9 / operations, "ns/operation");
    }

    void BenchmarkRunner::run_memory()
    {
        using namespace memory;
        const Config& cfg = _config;
        Report& report = _report;

        const std::string group = "Memory ";
        const auto bandwidth = [](double bytes, const Sample& s)
            {
                return s.median_s > 0 ? bytes / s.median_s / 1e9 : 0.0;
            };

        // 1.row major i column major
        const std::size_t matrix_size = std::min<std::size_t>(2048, std::max<std::size_t>(128, static_cast<std::size_t>(std::sqrt(static_cast<double>(cfg.n)))));
        std::vector<u32> matrix(matrix_size * matrix_size);

        for (std::size_t i = 0; i < matrix.size(); ++i) 
            matrix[i] = static_cast<u32>(i % 101);

        std::uint64_t sum = 0;

        const auto row = measure(cfg,
            [&] {
                sum = traverse(matrix, matrix_size, true);
            },
            [&] {
                return static_cast<double>(sum);
            });

        const auto result_row = sum;

        const auto col = measure(cfg,
            [&] {
                sum = traverse(matrix, matrix_size, false);
            },
            [&] {
                return static_cast<double>(sum); 
            });

        if (sum != result_row)
            throw std::runtime_error("Matrix sum error");

        report.add(group + " / " + "Row-major", row, bandwidth((double)matrix.size() * sizeof(u32), row), "GB/s");
        report.add(group + " / " + "Column-major", col, bandwidth((double)matrix.size() * sizeof(u32), col), "GB/s");


        // 2.matrix mult
        const std::size_t n = cfg.matrix;

        std::vector<float> a(n * n), b(n * n), c(n * n), reference(n * n);

        fill_floats(a.data(), a.size(), 100); 
        fill_floats(b.data(), b.size(), 200);
        matmul(a.data(), b.data(), reference.data(), n, 0);

        constexpr const char* names[] = { "ijk","ikj","jik","jki","kij","kji" };

        auto verify = [&]
            {
                float max_error = 0, max_reference = 0;
                for (std::size_t i = 0; i < c.size(); ++i) 
                {
                    max_error = std::max(max_error, std::fabs(c[i] - reference[i]));
                    max_reference = std::max(max_reference, std::fabs(reference[i]));
                }
                if (max_error > 0.002f * std::max(1.0f, max_reference))
                    throw std::runtime_error("Matrix mult not correct");
                return max_error;
            };


        for (int order = 0; order < 6; ++order)
        {
            matmul(a.data(), b.data(), c.data(), n, order);
            verify();

            const auto s = measure(cfg,
                [&] {
                    matmul(a.data(), b.data(), c.data(), n, order);
                },
                [&] {
                    return checksum(c.data(), c.size());
                });

            report.add(group + " / " + (std::string("MatMul order ") + names[order]), s, (2.0 * n * n * n) / s.median_s / 1e9, "GFLOP/s");
        }

        for (std::size_t block : {8u, 16u, 32u, 64u})
        {
            matmul_tiled(a.data(), b.data(), c.data(), n, block);

            verify();

            const auto s = measure(cfg, 
                [&] {
                    matmul_tiled(a.data(), b.data(), c.data(), n, block); 
                },
                [&] {
                    return checksum(c.data(), c.size()); 
                });

            report.add(group + " / " + ("MatMul tiled B=" + std::to_string(block)), s, (2.0 * n * n * n) / s.median_s / 1e9, "GFLOP/s");
        }
    }

    void BenchmarkRunner::run_vectorization()
    {
        using namespace simd;
        const Config& cfg = _config;
        Report& report = _report;

        std::cout << "Vektorizacija/SIMD:\n";
        const std::string group = "Vectorization:";
        const std::size_t n = cfg.n + 3;
        const bool avx = avx2_supported();

        std::cout << "AVX2: " << (avx ? "yes" : "no") << "\n";
#if !defined(PERFLABS_HAS_OPENMP)
        std::cout << "No OpenMP\n";
#endif
        std::vector<float> x(n), initial(n), y(n), expected(n), destination(n);
        fill_floats(x.data(), n, 12345);
        fill_floats(initial.data(), n, 54321);

        for (std::size_t i = 0; i < n; ++i)
        {
            x[i] = std::fabs(x[i]) * 0.2f + 0.1f;
            initial[i] = std::fabs(initial[i]) * 0.2f + 0.1f;
        }

        const float scale = 0.7f;
        expected = initial;

        vectorization::saxpy_scalar(x.data(), expected.data(), n, scale);

        const auto saxpy_expected = expected;

        struct SaxpyEntry 
        { 
            const char* name; 
            vectorization::Saxpy function;
        };

        std::vector<SaxpyEntry> implementations =
        {
            {"SAXPY: scalar",vectorization::saxpy_scalar},
            {"SAXPY: auto",vectorization::saxpy_auto},
            {"SAXPY: omp simd",vectorization::saxpy_omp},
        };

        if (avx)
            implementations.push_back({ "SAXPY: AVX2 intrinsics",vectorization::saxpy_avx2 });

        for (const auto& impl : implementations)
        {
            y = initial;
            impl.function(x.data(), y.data(), n, scale);
            check_vector(expected, y.data(), n);
            y = initial;

            const auto s = measure(cfg, 
                [&] {
                    impl.function(x.data(), y.data(), n, scale); 
                },
                [&] {
                    return checksum(y.data(), n); 
                });

            report.add(group + "/" + impl.name, s, (12.0 * n) / s.median_s / 1e9, "GB/s");
        }


        
        double high_precision = 0;
        for (std::size_t i = 0; i < n; ++i)
            high_precision += static_cast<double>(x[i]) * initial[i];

        struct DotEntry 
        { 
            const char* name;
            vectorization::Dot function; 
        };

        std::vector<DotEntry> dots = 
        {
            {"Dot: scalar",vectorization::dot_scalar},
            {"Dot: auto",vectorization::dot_auto},
            {"Dot: omp simd",vectorization::dot_omp},
        };

        if (avx)
            dots.push_back({ "Dot: AVX2 intrinsics",vectorization::dot_avx2 });

        for (const auto& impl : dots) 
        {
            float dot = impl.function(x.data(), initial.data(), n);

            const double relative_error = std::fabs(static_cast<double>(dot) - high_precision) / std::max(1.0, std::fabs(high_precision));
            if (relative_error > 0.005)
                throw std::runtime_error(std::string("Dot product, accuracy wrong: ") + impl.name);

            const auto s = measure(cfg,
                [&] {
                    dot = impl.function(x.data(), initial.data(), n); 
                },
                [&] {
                    return static_cast<double>(dot); 
                });

            report.add(group + " / " + impl.name, s, 2.0 * n / s.median_s / 1e9, "GFLOP/s");
        }



        const float threshold = 0.2f;
        vectorization::threshold_scalar(x.data(), expected.data(), n, threshold, scale);

        struct ThresholdEntry 
        { 
            const char* name; 
            vectorization::Threshold function;
        };

        std::vector<ThresholdEntry> thresholds = 
        {
            {"Threshold: scalar",vectorization::threshold_scalar},
            {"Threshold: auto",vectorization::threshold_auto},
            {"Threshold: omp simd",vectorization::threshold_omp},
        };

        if (avx)
            thresholds.push_back({ "Threshold: AVX2 intrinsics",vectorization::threshold_avx2});

        for (const auto& impl : thresholds) 
        {
            impl.function(x.data(), destination.data(), n, threshold, scale);

            check_vector(expected, destination.data(), n);

            const auto s = measure(cfg,
                [&] {
                    impl.function(x.data(), destination.data(), n, threshold, scale);
                },
                [&] {
                    return checksum(destination.data(), n);
                });

            report.add(group + " / " + impl.name, s, n / s.median_s / 1e6, "Melems/s");
        }


        const std::size_t escape_n = std::min<std::size_t>(n, 123123);
        std::vector<float> cx(escape_n), cy(escape_n), ref(escape_n), out(escape_n);

        fill_floats(cx.data(), escape_n, 155); fill_floats(cy.data(), escape_n, 255);

        vectorization::escape_scalar(cx.data(), cy.data(), ref.data(), escape_n, 48);

        const vectorization::Escape escapes[] = { vectorization::escape_scalar, vectorization::escape_auto };
        const char* escape_names[] = { "Escape: scalar","Escape: auto" };

        for (int i = 0; i < 2; ++i) 
        {
            escapes[i](cx.data(), cy.data(), out.data(), escape_n, 48);

            check_vector(ref, out.data(), escape_n, 0.0f);

            const auto s = measure(cfg,
                [&] {
                    escapes[i](cx.data(), cy.data(), out.data(), escape_n, 48); 
                },
                [&] {
                    return checksum(out.data(), out.size()); 
                });

            report.add(group + " / " + escape_names[i], s, escape_n / s.median_s / 1e6, "Mpts/s");
        }


        {
            std::vector<float> a(n), b(n), output(n), correct(n), source2(2 * n);

            fill_floats(a.data(), n, 809); fill_floats(b.data(), n, 100);
            fill_floats(source2.data(), source2.size(), 111);

            for (std::size_t i = 0; i < n; ++i)
                correct[i] = a[i] + 2.f * b[i];

            const std::array fnsAlias = {
                std::pair{"Fn: aliasing unknown", &vectorization::fn_may_alias},
                std::pair{"Fn: restrict no-alias", &vectorization::fn_no_alias}
            };

            for (const auto& item : fnsAlias)
            {
                item.second(a.data(), b.data(), output.data(), n);

                check_vector(correct, output.data(), n);

                const auto t = measure(cfg, 
                    [&] {
                        item.second(a.data(), b.data(), output.data(), n);
                    }, 
                    [&] {
                        return checksum(output.data(), n);
                    });

                report.add(group + " / " + item.first, t, n / t.median_s / 1e6, "Melems/s");
            }


            std::vector<unsigned> prefix_input(n, 1u), prefix_output(n, 0u);
            vectorization::prefix_scan(prefix_input.data(), prefix_output.data(), n);

            if (prefix_output[n - 1] != n)
                throw std::runtime_error("Prefix scan error");

            const auto t = measure(cfg,
                [&] {
                    vectorization::prefix_scan(prefix_input.data(), prefix_output.data(), n);
                },
                [&] {
                    double z = 0; for (unsigned v : prefix_output)z += v; return z;
                });

            report.add(group + " / " + "Prefix scan", t, n / t.median_s / 1e6, "Melems/s");
        }

        if (avx) 
        {
            AlignedFloats aligned_x(n + 16), aligned_y(n + 16), unaligned_x(n + 16), unaligned_y(n + 16);

            float *ax = aligned_x.data(), *ay = aligned_y.data();
            float *ux = unaligned_x.data() + 1, *uy = unaligned_y.data() + 1;

            for (std::size_t i = 0; i < n; ++i) 
            { 
                ax[i] = ux[i] = x[i]; 
                ay[i] = uy[i] = initial[i];
            }

            vectorization::saxpy_avx2_aligned(ax, ay, n, scale);
            vectorization::saxpy_avx2(ux, uy, n, scale)
                ;
            check_vector(saxpy_expected, ay, n); 
            check_vector(saxpy_expected, uy, n);

            for (std::size_t i = 0; i < n; ++i) 
            {
                ay[i] = uy[i] = initial[i];
            }

            const auto a = measure(cfg, 
                [&] {
                    vectorization::saxpy_avx2_aligned(ax, ay, n, scale);
                },
                [&] {
                    return checksum(ay, n); 
                });

            const auto u = measure(cfg, 
                [&] {
                    vectorization::saxpy_avx2(ux, uy, n, scale); 
                },
                [&] {
                    return checksum(uy, n); 
                });

            report.add(group + " / " + "AVX2 aligned ", a, 12.0 * n / a.median_s / 1e9, "GB/s");
            report.add(group + " / " + "AVX2 unaligned ", u, 12.0 * n / u.median_s / 1e9, "GB/s");
        }

    }

    void BenchmarkRunner::run_particle_update()
    {
        using namespace particle;
        const Config& cfg = _config;
        Report& report = _report;

        const std::string group = "Particles: ";
        const std::size_t n = cfg.n + 3;

        constexpr float dt = 0.015f, damping = 0.996f, bounce = 0.8f, limit = 10.0f;

        std::vector<particle::Particle> initial(n);

        std::uint64_t rng = 0xABCD;

        for (auto& p : initial)
        {
            const auto random01 = [&]()
                {
                    return static_cast<float>(next_random(rng) % 10001) / 10000.f;
                };

            p.x = -11.0f + 22.f * random01();
            p.y = random01();
            p.z = random01();

            p.vx = -3.f + 6.f * random01();
            p.vy = random01();
            p.vz = random01();

            p.acceleration = -0.5f + random01();
            p.mass = 0.5f + random01();
        }

        auto scalar_solve = initial;
        particle::step_aos_scalar(scalar_solve.data(), n, dt, damping, bounce, limit);

        using Step = void (*)(const float*, float*, float*, std::size_t, float, float, float, float);

        const auto check_step = [&](Step implementation)
            {
                SoA state = make_soa(initial);
                implementation(state.acc.data(), state.x.data(), state.vx.data(), n, dt, damping, bounce, limit);
                verify(scalar_solve, state);
            };

        check_step(particle::step_soa_branch);

        check_step(particle::step_soa_branchless);

        if (avx2_supported())
            check_step(particle::step_soa_avx2);

        constexpr int steps_per_run = 4;
        auto p = initial;
        const auto baseline = measure(cfg,
            [&] {
                for (int step = 0; step < steps_per_run; ++step)
                    particle::step_aos_scalar(p.data(), n, dt, damping, bounce, limit);
            },
            [&] {
                double total = 0; for (const auto& a : p) total += a.x + a.vx; return total;
            }
        );
        const double baseline_speed = n * steps_per_run / baseline.median_s / 1e6;
        report.add(group + "1. AoS scalar", baseline, baseline_speed, "Mupdates/s");

        struct Implementation
        {
            const char* name;
            Step function;
        };

        std::vector<Implementation> variants =
        {
            {"2. SoA scalar",particle::step_soa_scalar},
            {"3. SoA with branches",particle::step_soa_branch},
            {"4. SoA branchless",particle::step_soa_branchless},
        };

        if (avx2_supported())
            variants.push_back({ "5. SoA explicit AVX2",particle::step_soa_avx2 });

        variants.push_back({ "6. SoA SIMD",particle::step_soa_simd });

        for (const auto& impl : variants)
        {
            SoA state = make_soa(initial);
            const auto s = measure(cfg,
                [&] {
                    for (int step = 0; step < steps_per_run; ++step)
                        impl.function(state.acc.data(), state.x.data(), state.vx.data(), n, dt, damping, bounce, limit);
                },
                [&] {
                    return checksum(state.x.data(), n) + checksum(state.vx.data(), n);
                }
            );
            report.add(group + impl.name, s, n * steps_per_run / s.median_s / 1e6, "Mupdates/s");
        }
    }
}
