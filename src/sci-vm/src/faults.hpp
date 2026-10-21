// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the interpreter reports errors. Where ScummVM stops the engine with
// error() (a script bug it has no workaround for, a broken resource), the
// interpreter records the first such fault and stops running scripts; the
// caller gets the message. Warnings go to the log (category Vm).

#pragma once

#include "sci/core/log.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sci::vm {

class Faults {
public:
    void raise(std::string message) {
        if (!message_) {
            message_ = std::move(message);
        }
    }

    [[nodiscard]] bool raised() const { return message_.has_value(); }

    [[nodiscard]] const std::string& message() const { return *message_; }

private:
    std::optional<std::string> message_;
};

inline void warn(std::string_view message) {
    core::log(core::LogLevel::Warning, core::LogCategory::Vm, message);
}

inline void debug(std::string_view message) {
    if (core::log_category_enabled(core::LogCategory::Vm)) {
        core::log(core::LogLevel::Debug, core::LogCategory::Vm, message);
    }
}

} // namespace sci::vm
