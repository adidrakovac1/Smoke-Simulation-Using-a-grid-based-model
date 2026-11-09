#pragma once

// Shared test runner. Used by smoke_tests (tests/test_main.cpp) and by "smoke_cli --selftest",
// so both run exactly the same registered tests (Windows Application Control on some machines
// blocks smoke_tests.exe but not smoke_cli.exe).

#include <ostream>

namespace smoke_test {

// Runs every doctest TEST_CASE linked into the program, prints doctest's report followed by an
// ASCII summary in Bosnian (docs/plan/CLI_OUTPUT.md, decision D7).
// argc/argv are optional doctest options (e.g. -tc="field3d*" to run a subset).
// Returns the process exit code: 0 if all tests pass, 1 otherwise.
int runAll(std::ostream& out, int argc = 0, const char* const* argv = nullptr);

} // namespace smoke_test
