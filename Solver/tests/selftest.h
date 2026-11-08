#pragma once

// Shared test runner. Used by smoke_tests (tests/test_main.cpp) and by "smoke_cli --selftest",
// so both run exactly the same registered tests (Windows Application Control on some machines
// blocks smoke_tests.exe but not smoke_cli.exe).

#include <ostream>

namespace smoke_test {

// Runs every test registered with SMOKE_TEST, prints PROSAO/PAO per test and a summary
// (ASCII, docs/plan/CLI_OUTPUT.md). Returns the process exit code: 0 if all pass, 1 otherwise.
int runAll(std::ostream& out);

} // namespace smoke_test
