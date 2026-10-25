#include "harness.hpp"

namespace glt::test {

int g_failures = 0;
std::string g_current_case;

std::vector<Case>& registry() {
    // Function-local static: the registrars run before main, and a namespace
    // scope global would be initialised in an unspecified order relative to
    // them.
    static std::vector<Case> cases;
    return cases;
}

void fail(const char* file, int line, const std::string& detail) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s\n    at %s:%d\n    %s\n", g_current_case.c_str(), file,
                 line, detail.c_str());
}

int run_all() {
    auto& cases = registry();

    int failed_cases = 0;
    int case_failures_at_start = 0;

    for (const auto& c : cases) {
        g_current_case = std::string(c.suite) + "." + c.name;
        case_failures_at_start = g_failures;
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::fprintf(stderr, "  THREW %s: %s\n", g_current_case.c_str(), e.what());
        } catch (...) {
            ++g_failures;
            std::fprintf(stderr, "  THREW %s: unknown exception\n", g_current_case.c_str());
        }

        if (g_failures > case_failures_at_start) {
            ++failed_cases;
            std::fprintf(stderr, "  ^ %s FAILED\n\n", g_current_case.c_str());
        } else {
            std::fprintf(stderr, "  ok   %s\n", g_current_case.c_str());
        }
    }

    std::fprintf(stderr, "\n%d test(s), %d failed case(s), %d assertion failure(s)\n",
                 static_cast<int>(cases.size()), failed_cases, g_failures);
    return g_failures;
}

} // namespace glt::test

int main() {
    return glt::test::run_all();
}