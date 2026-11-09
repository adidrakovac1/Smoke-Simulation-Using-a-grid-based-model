#pragma once
// Side-by-side comparison of kernel versions (Jacobi, advection), shared by smoke_lab and smoke_demo.
// Style of the lab example (docs/reference/PRS_Vektorizacija: BenchmarkRunner::run(), measure(), check_vector):
// every version is first checked against version 1 bit by bit, then measured in interleaved rounds (D10).

#include <vector>

namespace smoke_lab {

struct LabOptions {
    std::vector<int> grids{32, 64, 96, 128};
    int rounds = 3;       // interleaved rounds (decision D10)
    int repeats = 10;     // timed runs per version and round
    int warmup = 1;
    int iterations = 40;  // Jacobi iterations per run, as in one simulation step (SmokeParams default)
};

void runJacobi(const LabOptions& o);
void runAdvection(const LabOptions& o);

/// "Kako citati" note printed after the tables.
void printReadingNote();

} // namespace smoke_lab
