#pragma once

#include "CpuFeatures.h"
#include "stages/Stages.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <immintrin.h>
#define SMOKE_CPU_RELAX() _mm_pause()
#else
#define SMOKE_CPU_RELAX() std::this_thread::yield()
#endif

namespace smoke {

/// Barrier for a fixed team of threads that busy-waits instead of sleeping (decision D19).
///
/// std::barrier (and a mutex + condition variable) put a waiting thread to sleep; waking it again costs the
/// operating system tens of microseconds. The Jacobi wavefront (stages/JacobiWavefront.cpp) meets at a barrier
/// several hundred times per step, a few microseconds apart, so it needs a barrier that answers in well under a
/// microsecond: every thread counts itself in with one atomic decrement and then spins (with the CPU "pause"
/// hint, which tells the core that this is a wait loop) until the last one flips the shared "phase" number.
/// If a thread of the team is not running (the operating system gave its processor to another program), the
/// others would spin for a whole scheduler time slice; so after kSpinsBeforeYield polls a waiting thread yields
/// its processor on every further poll.
///
/// D20: with `blocking` set the waiting threads sleep instead (C++20 std::atomic::wait / notify_all): no
/// cycles are spent while waiting, so they do not show up in the CPI, but every wake-up takes the operating
/// system microseconds (SolverConfig::threadWait, ThreadWait::Block).
class SpinBarrier {
public:
    explicit SpinBarrier(int threads = 1) : threads_(threads), remaining_(threads) {}

    void reset(int threads, bool blocking = false) {
        threads_  = threads;
        blocking_ = blocking;
        remaining_.store(threads, std::memory_order_relaxed);
        phase_.store(0, std::memory_order_relaxed);
    }

    /// All writes made by any thread before arrive_and_wait() are visible to every thread after it
    /// (acquire/release on remaining_ and phase_).
    void arrive_and_wait() noexcept {
        if (threads_ <= 1) return;
        const std::uint32_t phase = phase_.load(std::memory_order_relaxed);
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            remaining_.store(threads_, std::memory_order_relaxed);
            phase_.store(phase + 1, std::memory_order_release);
            if (blocking_) phase_.notify_all();
            return;
        }
        if (blocking_) {
            while (phase_.load(std::memory_order_acquire) == phase) phase_.wait(phase, std::memory_order_acquire);
            return;
        }
        int polls = 0;
        while (phase_.load(std::memory_order_acquire) == phase) {
            if (polls < kSpinsBeforeYield) {
                ++polls;
                SMOKE_CPU_RELAX();
            } else {
                std::this_thread::yield();
            }
        }
    }

private:
    // TODO(measure): about 20 to 50 us of polling on PC-2 (one pause is ~65 cycles on Zen 3), longer than the
    // wait at a balanced barrier, much shorter than a scheduler time slice (~15 ms on Windows).
    static constexpr int kSpinsBeforeYield = 4096;
    int threads_;
    bool blocking_ = false;
    // Separate cache lines, so the spinning readers of phase_ do not slow down the decrements.
    alignas(64) std::atomic<int> remaining_;
    alignas(64) std::atomic<std::uint32_t> phase_{0};
};

/// Persistent worker threads for SmokeSolver::step (decision D18, extended by D19). Spawns (numThreads - 1)
/// workers; the calling thread works as thread 0.
///
/// One parallelRun() per simulation step: the whole step runs inside it and the phases are separated by
/// barrier() calls. Between two runs a worker first spins for a short time (consecutive steps of a benchmark
/// follow each other within microseconds) and only then sleeps on a condition variable (the visualization
/// calls step() once per frame, so the workers do not burn a core between frames).
class ThreadPool {
public:
    /// blocking (D20): waiting threads sleep instead of spinning, at the barrier, between steps and while the
    /// calling thread waits for the workers at the end of parallelRun.
    explicit ThreadPool(int requestedThreads = 0, bool blocking = false) : blocking_(blocking) {
        int count = requestedThreads;
        if (count <= 0) count = defaultThreadCount();
        numThreads_ = count;
        barrier_.reset(numThreads_, blocking_);

        for (int i = 1; i < numThreads_; ++i) {
            workers_.emplace_back([this, i] { workerLoop(i); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_.store(true, std::memory_order_release);
        }
        cvWork_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    int numThreads() const noexcept { return numThreads_; }

    /// The barrier of this team (all numThreads() threads must call it).
    void barrier() noexcept { barrier_.arrive_and_wait(); }

    /// Executes `func(int threadId)` on all numThreads_ threads (thread 0 = calling thread) and returns when
    /// all of them have finished.
    template <typename F>
    void parallelRun(F&& func) {
        if (numThreads_ <= 1) {
            func(0);
            return;
        }
        auto wrapper = [](int tid, void* ctx) { (*static_cast<std::remove_reference_t<F>*>(ctx))(tid); };
        taskFunc_ = wrapper;
        taskCtx_  = const_cast<void*>(static_cast<const void*>(&func));
        remaining_.store(numThreads_ - 1, std::memory_order_relaxed);
        {
            // The lock orders the generation change with a worker that is just going to sleep.
            std::lock_guard<std::mutex> lock(mutex_);
            generation_.fetch_add(1, std::memory_order_release);
        }
        if (sleepers_.load(std::memory_order_acquire) > 0) cvWork_.notify_all();
        func(0);
        if (blocking_) {
            for (int left = remaining_.load(std::memory_order_acquire); left != 0;
                 left = remaining_.load(std::memory_order_acquire))
                remaining_.wait(left, std::memory_order_acquire);
            return;
        }
        for (int polls = 0; remaining_.load(std::memory_order_acquire) != 0;) {
            if (polls < 4096) {
                ++polls;
                SMOKE_CPU_RELAX();
            } else {
                std::this_thread::yield();
            }
        }
    }

    /// Partitions nz into disjoint z-slabs and calls `kernel(SlabRange slab, int threadId)`
    /// on each thread. Covers [1, nz + 1) with no gaps and no overlap.
    template <typename F>
    void parallelSlabs(int nz, F&& kernel) {
        if (numThreads_ <= 1 || nz <= 1) {
            kernel(serial::SlabRange{1, nz + 1}, 0);
            return;
        }
        parallelRun([&](int tid) {
            const serial::SlabRange slab = slabOf(nz, tid, numThreads_);
            if (slab.k0 < slab.k1) kernel(slab, tid);
        });
    }

    /// Slab of thread `tid` out of `threads`: [1 + tid*n/threads, 1 + (tid+1)*n/threads).
    static serial::SlabRange slabOf(int n, int tid, int threads) {
        return {1 + static_cast<int>((static_cast<long long>(tid) * n) / threads),
                1 + static_cast<int>((static_cast<long long>(tid + 1) * n) / threads)};
    }

    /// Default team size: one thread per PHYSICAL core. Measured on PC-2 (docs/plan/PERF_LOG.md, P-004): with all
    /// 12 logical processors the median is better at 64^3 (2.05 against 2.53 ms) but a few steps take 0.5 to 2 s:
    /// when any other program needs a processor it must displace one of the team's pinned threads, and the whole
    /// team then waits at the next barrier. With 6 threads no such step was seen, the second logical processor
    /// of every core stays free for the rest of the system (e.g. the graphics driver), and 128^3 is fastest.
    static int defaultThreadCount() { return physicalCoreCount(); }

private:
    void workerLoop(int id) {
        // Worker i runs on core i (the calling thread, which is the application's own thread, is not pinned;
        // core 0, or its first logical processor, is left for it). See pinCurrentThread.
        pinCurrentThread(id, numThreads_);
        std::uint64_t seen = 0;
        using Clock = std::chrono::steady_clock;
        while (true) {
            // Spin for a while: the next step usually starts right away (not when blocking, D20).
            const auto spinUntil = Clock::now() + std::chrono::microseconds(kSpinMicroseconds);
            std::uint64_t gen = generation_.load(std::memory_order_acquire);
            int polls = 0;
            while (!blocking_ && gen == seen && !stop_.load(std::memory_order_relaxed)) {
                SMOKE_CPU_RELAX();
                if (++polls == 256) {
                    polls = 0;
                    if (Clock::now() > spinUntil) break;
                }
                gen = generation_.load(std::memory_order_acquire);
            }
            if (gen == seen) {
                std::unique_lock<std::mutex> lock(mutex_);
                sleepers_.fetch_add(1, std::memory_order_acq_rel);
                cvWork_.wait(lock, [&] {
                    return stop_.load(std::memory_order_relaxed) ||
                           generation_.load(std::memory_order_acquire) != seen;
                });
                sleepers_.fetch_sub(1, std::memory_order_acq_rel);
                gen = generation_.load(std::memory_order_acquire);
            }
            if (stop_.load(std::memory_order_acquire)) return;
            seen = gen;
            taskFunc_(id, taskCtx_);
            if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1 && blocking_) remaining_.notify_one();
        }
    }

    static constexpr int kSpinMicroseconds = 2000;

    bool blocking_ = false;
    int numThreads_ = 1;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cvWork_;
    void (*taskFunc_)(int, void*) = nullptr;
    void* taskCtx_ = nullptr;
    alignas(64) std::atomic<std::uint64_t> generation_{0};
    alignas(64) std::atomic<int> remaining_{0};
    std::atomic<int> sleepers_{0};
    std::atomic<bool> stop_{false};
    SpinBarrier barrier_;
};

} // namespace smoke
