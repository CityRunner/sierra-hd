// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/log.hpp"
#include "sci_test.hpp"

#include <string>

using sci::core::LogCategory;
using sci::core::LogLevel;

namespace {

struct Received {
    int count = 0;
    std::string last_message;
};

Received& received() {
    static Received state;
    return state;
}

void count_messages(LogLevel /*level*/, LogCategory /*category*/, std::string_view message) noexcept {
    ++received().count;
    received().last_message = message;
}

} // namespace

SCI_TEST(log_messages_reach_the_sink_for_enabled_categories) {
    const sci::core::LogSink previous = sci::core::set_log_sink(&count_messages);
    received() = Received{};

    sci::core::log(LogLevel::Info, LogCategory::General, "general");
    SCI_CHECK(received().count == 1 && received().last_message == "general");

    sci::core::log(LogLevel::Info, LogCategory::Resources, "off by default");
    SCI_CHECK(received().count == 1);

    sci::core::set_log_category_enabled(LogCategory::Resources, true);
    sci::core::log(LogLevel::Warning, LogCategory::Resources, "resources");
    SCI_CHECK(received().count == 2 && received().last_message == "resources");

    sci::core::set_log_category_enabled(LogCategory::Resources, false);
    sci::core::set_log_category_enabled(LogCategory::General, false); // General stays on.
    sci::core::log(LogLevel::Error, LogCategory::General, "still on");
    SCI_CHECK(received().count == 3);

    sci::core::set_log_sink(previous);
}
