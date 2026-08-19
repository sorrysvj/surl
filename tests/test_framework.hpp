#pragma once

// A deliberately small test framework. SURL vendors no dependencies, and a
// test runner is not where that policy should first be broken: this is a few
// dozen lines, builds instantly, and works identically on every toolchain.

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace surltest {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> body);
};

/// Thrown by a failing assertion; the runner catches it and reports the case.
struct AssertionFailure {
    std::string message;
};

[[noreturn]] void fail(const std::string& message, const char* file, int line);

/// Runs every registered case. Pass a substring as argv[1] to filter.
int run_all(int argc, char** argv);

/// Formats a value for assertion messages, falling back to "<?>" for types
/// that cannot be streamed.
template <typename T>
std::string describe(const T& value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

inline std::string describe(bool value) { return value ? "true" : "false"; }
inline std::string describe(std::nullptr_t) { return "nullptr"; }

} // namespace surltest

#define SURL_TEST(suite_name, case_name)                                                 \
    static void suite_name##_##case_name##_body();                                       \
    static ::surltest::Registrar suite_name##_##case_name##_registrar(                   \
        #suite_name, #case_name, suite_name##_##case_name##_body);                       \
    static void suite_name##_##case_name##_body()

#define CHECK(condition)                                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            ::surltest::fail("expected: " #condition, __FILE__, __LINE__);               \
        }                                                                                \
    } while (false)

#define CHECK_FALSE(condition) CHECK(!(condition))

#define CHECK_EQ(actual, expected)                                                        \
    do {                                                                                  \
        const auto& surl_actual = (actual);                                               \
        const auto& surl_expected = (expected);                                           \
        if (!(surl_actual == surl_expected)) {                                            \
            ::surltest::fail(std::string("expected " #actual " == " #expected "\n") +     \
                                 "    actual:   " + ::surltest::describe(surl_actual) +   \
                                 "\n    expected: " +                                     \
                                 ::surltest::describe(surl_expected),                     \
                             __FILE__, __LINE__);                                         \
        }                                                                                 \
    } while (false)

#define CHECK_NE(actual, unexpected)                                                      \
    do {                                                                                  \
        if ((actual) == (unexpected)) {                                                    \
            ::surltest::fail("expected " #actual " != " #unexpected, __FILE__, __LINE__); \
        }                                                                                  \
    } while (false)

#define CHECK_CONTAINS(haystack, needle)                                                  \
    do {                                                                                  \
        const std::string surl_haystack = (haystack);                                     \
        const std::string surl_needle = (needle);                                         \
        if (surl_haystack.find(surl_needle) == std::string::npos) {                        \
            ::surltest::fail(std::string("expected to find \"") + surl_needle +           \
                                 "\" in:\n" + surl_haystack,                              \
                             __FILE__, __LINE__);                                          \
        }                                                                                  \
    } while (false)

#define CHECK_NOT_CONTAINS(haystack, needle)                                              \
    do {                                                                                  \
        const std::string surl_haystack = (haystack);                                     \
        const std::string surl_needle = (needle);                                         \
        if (surl_haystack.find(surl_needle) != std::string::npos) {                        \
            ::surltest::fail(std::string("did not expect to find \"") + surl_needle +     \
                                 "\" in:\n" + surl_haystack,                              \
                             __FILE__, __LINE__);                                          \
        }                                                                                  \
    } while (false)
