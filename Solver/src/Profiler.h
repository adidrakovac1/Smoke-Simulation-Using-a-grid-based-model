#pragma once

#include <chrono>

namespace smoke {

/// Measures the time between construction and destruction and ADDS it (in milliseconds) to `outMs`.
/// Wrap exactly the code you want to time, and keep printing outside of it
/// (docs/plan/CLI_OUTPUT.md, principle 3).
class ScopedTimer {
public:
    explicit ScopedTimer(double& outMs) : out_(outMs), start_(Clock::now()) {}
    ~ScopedTimer() {
        out_ += std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }
    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    using Clock = std::chrono::steady_clock;
    double&           out_;
    Clock::time_point start_;
};

} // namespace smoke
