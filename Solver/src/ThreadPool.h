#pragma once

#include "stages/Stages.h"

#include <barrier>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace smoke {

/// Lightweight, persistent worker thread pool designed specifically for Eulerian grid
/// slab-based parallel execution. Spawns (numThreads - 1) background worker threads.
/// The calling/main thread acts as worker 0, eliminating thread waste and dispatch latency.
class ThreadPool {
public:
    explicit ThreadPool(int requestedThreads = 0) {
        int count = requestedThreads;
        if (count <= 0) {
            count = static_cast<int>(std::thread::hardware_concurrency());
            if (count <= 0) count = 1;
        }
        numThreads_ = count;

        for (int i = 1; i < numThreads_; ++i) {
            workers_.emplace_back([this, i] {
                uint64_t localGen = 0;
                while (true) {
                    void (*func)(int, void*) = nullptr;
                    void* ctx = nullptr;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cvWork_.wait(lock, [this, &localGen] {
                            return stop_ || generation_ > localGen;
                        });
                        if (stop_) return;
                        localGen = generation_;
                        func = taskFunc_;
                        ctx = taskCtx_;
                    }
                    if (func) func(i, ctx);
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        if (--remainingWorkers_ == 0) {
                            cvDone_.notify_one();
                        }
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
            cvWork_.notify_all();
        }
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    int numThreads() const noexcept { return numThreads_; }

    /// Executes `func(int threadId)` on all numThreads_ threads (thread 0 = calling thread).
    template <typename F>
    void parallelRun(F&& func) {
        if (numThreads_ <= 1) {
            func(0);
            return;
        }
        auto wrapper = [](int tid, void* ctx) {
            (*static_cast<std::decay_t<F>*>(ctx))(tid);
        };
        {
            std::unique_lock<std::mutex> lock(mutex_);
            taskFunc_ = wrapper;
            taskCtx_ = &func;
            remainingWorkers_ = numThreads_ - 1;
            ++generation_;
            cvWork_.notify_all();
        }
        func(0);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cvDone_.wait(lock, [this] { return remainingWorkers_ == 0; });
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
            const int k0 = 1 + (tid * nz) / numThreads_;
            const int k1 = 1 + ((tid + 1) * nz) / numThreads_;
            if (k0 < k1) {
                kernel(serial::SlabRange{k0, k1}, tid);
            }
        });
    }

private:
    int numThreads_ = 1;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cvWork_;
    std::condition_variable cvDone_;
    void (*taskFunc_)(int, void*) = nullptr;
    void* taskCtx_ = nullptr;
    int remainingWorkers_ = 0;
    uint64_t generation_ = 0;
    bool stop_ = false;
};

} // namespace smoke
