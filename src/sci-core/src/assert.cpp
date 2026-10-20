// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/assert.hpp"

#include <atomic>
#include <cstdlib>

namespace sci::core {

namespace {

std::atomic<AssertionHandler>& assertion_handler() noexcept {
    static std::atomic<AssertionHandler> handler{nullptr};
    return handler;
}

} // namespace

AssertionHandler set_assertion_handler(AssertionHandler handler) noexcept {
    return assertion_handler().exchange(handler);
}

void assertion_failed(std::string_view expression, const std::source_location& location) noexcept {
    if (const AssertionHandler handler = assertion_handler().load(); handler != nullptr) {
        handler(expression, location);
    }
    std::abort();
}

} // namespace sci::core
