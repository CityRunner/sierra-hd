// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project's single logging facility (spec section 5.1). sci-core does no
// I/O: messages go to a sink that app (or a tool) installs. Without a sink,
// messages are dropped. Categories other than General are off by default and
// are switched on by the sink's owner; trace categories stay compiled into
// release builds.

#pragma once

#include <cstdint>
#include <string_view>

namespace sci::core {

enum class LogLevel : std::int8_t {
    Error,
    Warning,
    Info,
    Debug,
};

enum class LogCategory : std::int8_t {
    General,   // Always enabled.
    Resources, // Resource loading: skipped map entries, patch files, failed loads.
    Vm,        // Script execution: script bugs the interpreter works around, unusual kernel calls.
};

using LogSink = void (*)(LogLevel level, LogCategory category, std::string_view message) noexcept;

// Installs the sink and returns the previous one. Thread-safe.
LogSink set_log_sink(LogSink sink) noexcept;

// Enables or disables a category. General cannot be disabled. Thread-safe.
void set_log_category_enabled(LogCategory category, bool enabled) noexcept;

[[nodiscard]] bool log_category_enabled(LogCategory category) noexcept;

// Passes the message to the sink if one is installed and the category is
// enabled. Callers format the message themselves (std::format).
void log(LogLevel level, LogCategory category, std::string_view message) noexcept;

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;
[[nodiscard]] std::string_view to_string(LogCategory category) noexcept;

} // namespace sci::core
