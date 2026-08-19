#include "test_framework.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>

namespace surltest {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

Registrar::Registrar(const char* suite, const char* name, std::function<void()> body) {
    registry().push_back(TestCase{suite, name, std::move(body)});
}

void fail(const std::string& message, const char* file, int line) {
    std::string text = message;
    text += "\n    at ";
    text += file;
    text += ":";
    text += std::to_string(line);
    throw AssertionFailure{text};
}

int run_all(int argc, char** argv) {
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    std::vector<TestCase>& cases = registry();
    std::stable_sort(cases.begin(), cases.end(), [](const TestCase& a, const TestCase& b) {
        return a.suite < b.suite;
    });

    std::size_t passed = 0;
    std::vector<std::string> failures;
    const auto started = std::chrono::steady_clock::now();

    for (const TestCase& test : cases) {
        const std::string full = test.suite + "." + test.name;
        if (filter != nullptr && full.find(filter) == std::string::npos) continue;

        try {
            test.body();
            ++passed;
            std::printf("  ok    %s\n", full.c_str());
        } catch (const AssertionFailure& failure) {
            failures.push_back(full + "\n    " + failure.message);
            std::printf("  FAIL  %s\n", full.c_str());
        } catch (const std::exception& ex) {
            failures.push_back(full + "\n    threw std::exception: " + ex.what());
            std::printf("  FAIL  %s (exception)\n", full.c_str());
        } catch (...) {
            failures.push_back(full + "\n    threw an unknown exception");
            std::printf("  FAIL  %s (exception)\n", full.c_str());
        }
        std::fflush(stdout);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();

    std::printf("\n");
    if (failures.empty()) {
        std::printf("%zu passed in %lldms\n", passed, static_cast<long long>(elapsed));
        return 0;
    }

    std::printf("FAILURES (%zu):\n\n", failures.size());
    for (const std::string& failure : failures) {
        std::printf("  %s\n\n", failure.c_str());
    }
    std::printf("%zu passed, %zu failed\n", passed, failures.size());
    return 1;
}

} // namespace surltest

int main(int argc, char** argv) { return surltest::run_all(argc, argv); }
