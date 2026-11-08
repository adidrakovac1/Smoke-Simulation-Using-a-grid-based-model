#include "selftest.h"

#include <iostream>

int main() {
    std::cout << "=== Smoke Solver: testovi tacnosti (smoke_tests) ===\n\n";
    return smoke_test::runAll(std::cout);
}
