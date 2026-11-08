#pragma once

// Tiny test helper so the skeleton has NO third-party dependency.
// Decision D5 (docs/plan/PLAN.md): the plan recommends doctest via FetchContent; when the team
// confirms it, this file is replaced by doctest and the tests are converted (they are small).

#include <iostream>
#include <vector>

namespace smoke_test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

} // namespace smoke_test

#define SMOKE_TEST(name)                                                 \
    static void name();                                                  \
    static ::smoke_test::Registrar registrar_##name{#name, &name};       \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            ++::smoke_test::failures();                                              \
            std::cerr << "  CHECK FAILED: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        }                                                                            \
    } while (0)
