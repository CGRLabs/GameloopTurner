// ─────────────────────────────────────────────────────────────────────────────
// Minimal test harness. Deliberately dependency-free: adding a test framework
// would pull another FetchContent into a project whose whole point is a small
// static binary, and the assertion surface needed here is a dozen lines.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace glt::test {

struct Case {
    const char* suite;
    const char* name;
    void (*fn)();
};

// Registry of every test, populated by the GLT_TEST macro at static-init time.
[[nodiscard]] std::vector<Case>& registry();

// Failure recorded by the most recent CHECK inside the running test.
extern int g_failures;
extern std::string g_current_case;

void fail(const char* file, int line, const std::string& detail);

// Runs everything registered so far. Returns the number of failures.
int run_all();

struct Registrar {
    Registrar(const char* suite, const char* name, void (*fn)()) {
        registry().push_back(Case{suite, name, fn});
    }
};

} // namespace glt::test

// ── Assertions ───────────────────────────────────────────────────────────────

// show() renders a mismatched value so a failure prints what was actually
// compared, not just two addresses. They live in glt::test because the macros
// below expand to ::glt::test::show(...) at the call site.
namespace glt::test {

inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(const char* v) { return std::string("\"") + (v ? v : "(null)") + "\""; }
inline std::string show(bool v) { return v ? "true" : "false"; }
inline std::string show(int v) { return std::to_string(v); }
inline std::string show(long v) { return std::to_string(v); }
inline std::string show(long long v) { return std::to_string(v); }
inline std::string show(unsigned v) { return std::to_string(v); }
inline std::string show(unsigned long v) { return std::to_string(v); }
inline std::string show(unsigned long long v) { return std::to_string(v); }
inline std::string show(std::string_view v) { return "\"" + std::string(v) + "\""; }
inline std::string show(char v) { return std::string(1, v); }

// Catch-all so a comparison against a type without an overload still prints
// something instead of failing to compile.
template <typename T>
inline std::string show(const T&) {
    return "<value>";
}

} // namespace glt::test

#define GLT_TEST(suite, name)                                                    \
    static void suite##_##name();                                                \
    static ::glt::test::Registrar reg_##suite##_##name(#suite, #name,            \
                                                       &suite##_##name);          \
    static void suite##_##name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) ::glt::test::fail(__FILE__, __LINE__, "CHECK failed: " #cond); \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        const auto _a = (a);                                                     \
        const auto _b = (b);                                                     \
        if (!((_a) == (_b))) {                                                   \
            ::glt::test::fail(__FILE__, __LINE__,                                \
                              std::string("CHECK_EQ failed: " #a " == " #b      \
                                          "\n      left  = ") +                 \
                                  ::glt::test::show(_a) + "\n      right = " +  \
                                  ::glt::test::show(_b));                       \
        }                                                                        \
    } while (0)

#define CHECK_NE(a, b)                                                           \
    do {                                                                         \
        if (((a) == (b))) {                                                      \
            ::glt::test::fail(__FILE__, __LINE__, "CHECK_NE failed: " #a " != " #b); \
        }                                                                        \
    } while (0)