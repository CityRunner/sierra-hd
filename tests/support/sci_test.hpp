// SPDX-License-Identifier: GPL-3.0-or-later
//
// A minimal test harness, so the unit tests need no external dependency.
//
//   SCI_TEST(reads_little_endian) {
//       SCI_CHECK(value == 0x1234);
//   }
//
// Each test executable runs all its registered tests in registration order
// and exits non-zero if any check failed.

#pragma once

#include <source_location>
#include <string_view>

namespace sci::test {

using TestFunction = void (*)();

struct Registrar {
    Registrar(std::string_view name, TestFunction function) noexcept;
};

void report_failure(std::string_view expression, const std::source_location& location) noexcept;

} // namespace sci::test

// NOLINTBEGIN(cppcoreguidelines-macro-usage): the harness needs the test name and expression text.
#define SCI_TEST(name)                                                                                                 \
    static void name();                                                                                                \
    static const ::sci::test::Registrar name##_registrar{#name, &(name)};                                              \
    static void name()

// Variadic, so that conditions with braced initialisers need no extra parentheses.
#define SCI_CHECK(...)                                                                                                 \
    ((__VA_ARGS__) ? static_cast<void>(0) : ::sci::test::report_failure(#__VA_ARGS__, std::source_location::current()))
// NOLINTEND(cppcoreguidelines-macro-usage)
