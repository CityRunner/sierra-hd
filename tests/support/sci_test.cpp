// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci_test.hpp"

#include <cstdio>
#include <print>
#include <vector>

namespace sci::test {

namespace {

struct TestCase {
    std::string_view name;
    TestFunction function;
};

std::vector<TestCase>& registry() noexcept {
    static std::vector<TestCase> tests;
    return tests;
}

int& failure_count() noexcept {
    static int count = 0;
    return count;
}

} // namespace

Registrar::Registrar(std::string_view name, TestFunction function) noexcept {
    registry().push_back(TestCase{.name = name, .function = function});
}

void report_failure(std::string_view expression, const std::source_location& location) noexcept {
    std::println(stderr, "{}:{}: check failed: {}", location.file_name(), location.line(), expression);
    ++failure_count();
}

} // namespace sci::test

int main() {
    using sci::test::failure_count;
    int failed_tests = 0;
    for (const auto& test : sci::test::registry()) {
        const int failures_before = failure_count();
        test.function();
        const bool passed = failure_count() == failures_before;
        if (!passed) {
            ++failed_tests;
        }
        std::println("[{}] {}", passed ? "pass" : "FAIL", test.name);
    }
    std::println("{} tests, {} failed", sci::test::registry().size(), failed_tests);
    return failed_tests == 0 ? 0 : 1;
}
