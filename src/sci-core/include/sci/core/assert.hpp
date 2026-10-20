// SPDX-License-Identifier: GPL-3.0-or-later
//
// Invariant checks. A failed SCI_ASSERT is a bug in this program, never a
// property of the input: input problems are reported as sci::core::Error.
// Assertions stay enabled in release builds.

#pragma once

#include <source_location>
#include <string_view>

namespace sci::core {

// Called with the failed expression before the program aborts. sci-core does
// no I/O, so app installs a handler that logs the failure.
using AssertionHandler = void (*)(std::string_view expression, const std::source_location& location) noexcept;

// Installs the handler and returns the previous one. Thread-safe.
AssertionHandler set_assertion_handler(AssertionHandler handler) noexcept;

[[noreturn]] void assertion_failed(std::string_view expression, const std::source_location& location) noexcept;

} // namespace sci::core

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): needs the expression text and the caller's location.
#define SCI_ASSERT(condition)                                                                                          \
    ((condition) ? static_cast<void>(0) : ::sci::core::assertion_failed(#condition, std::source_location::current()))
