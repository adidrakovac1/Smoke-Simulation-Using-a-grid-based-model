// The one translation unit that contains doctest's implementation (decision D5, docs/plan/PLAN.md).
// Every other test file only includes "doctest/doctest.h" and declares TEST_CASEs.
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "selftest.h"

namespace smoke_test {

namespace {

// Totals of the last run, filled by the listener below. Test-only code: doctest creates the listener
// itself, so this is the simplest way to hand the numbers back to runAll().
struct RunTotals {
    int testCases       = 0;
    int testCasesFailed = 0;
    int asserts         = 0;
    int assertsFailed   = 0;
};

RunTotals& lastTotals() {
    static RunTotals totals;
    return totals;
}

// A doctest "listener" receives the same events as a reporter but prints nothing; we only use it to
// read the final counts so we can print our own ASCII summary (docs/plan/CLI_OUTPUT.md, D7).
struct TotalsListener : doctest::IReporter {
    explicit TotalsListener(const doctest::ContextOptions&) {}

    void test_run_end(const doctest::TestRunStats& s) override {
        lastTotals() = {static_cast<int>(s.numTestCasesPassingFilters), static_cast<int>(s.numTestCasesFailed),
                        s.numAsserts, s.numAssertsFailed};
    }

    void report_query(const doctest::QueryData&) override {}
    void test_run_start() override {}
    void test_case_start(const doctest::TestCaseData&) override {}
    void test_case_reenter(const doctest::TestCaseData&) override {}
    void test_case_end(const doctest::CurrentTestCaseStats&) override {}
    void test_case_exception(const doctest::TestCaseException&) override {}
    void subcase_start(const doctest::SubcaseSignature&) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData&) override {}
    void log_message(const doctest::MessageData&) override {}
    void test_case_skipped(const doctest::TestCaseData&) override {}
};

} // namespace

DOCTEST_REGISTER_LISTENER("smoke_totals", 1, TotalsListener);

int runAll(std::ostream& out, int argc, const char* const* argv) {
    doctest::Context context;
    if (argc > 0 && argv != nullptr) context.applyCommandLine(argc, argv);  // e.g. -tc="advection*"
    context.setOption("no-breaks", true);  // never break into a debugger
    context.setCout(&out);

    const int result = context.run();
    if (context.shouldExit()) return result;  // --help, --list-test-cases, ...

    const RunTotals& t = lastTotals();
    out << "\nUkupno: " << t.testCases << " testova, proslo " << (t.testCases - t.testCasesFailed) << ", palo "
        << t.testCasesFailed << " (provjera: " << t.asserts << ", neuspjelih: " << t.assertsFailed << ")\n"
        << "Status: " << (result == 0 ? "OK" : "GRESKA (neki testovi su pali)") << "\n";
    return result == 0 ? 0 : 1;
}

} // namespace smoke_test
