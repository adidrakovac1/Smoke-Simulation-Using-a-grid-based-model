#include "selftest.h"

#include "check.h"

namespace smoke_test {

int runAll(std::ostream& out) {
    int total = 0;
    int failedTests = 0;
    for (const auto& t : registry()) {
        const int before = failures();
        t.fn();
        const bool ok = (failures() == before);
        out << (ok ? "  PROSAO  " : "  PAO     ") << t.name << "\n";
        ++total;
        if (!ok) ++failedTests;
    }
    out << "\nUkupno: " << total << " testova, proslo " << (total - failedTests) << ", palo " << failedTests
        << " (neuspjelih provjera: " << failures() << ")\n"
        << "Status: " << (failedTests == 0 ? "OK" : "GRESKA (neki testovi su pali)") << "\n";
    return failedTests == 0 ? 0 : 1;
}

} // namespace smoke_test
