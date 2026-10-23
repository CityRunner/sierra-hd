// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx_layout.hpp"

#include "gfx_ports.hpp"

#include <cstdint>
#include <format>
#include <iterator>

namespace sci::vm {

std::string quoted(std::string_view text) {
    std::string result = "\"";
    for (const char c : text) {
        const auto byte = static_cast<std::uint8_t>(c);
        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (byte < 32 || byte > 126) {
            std::format_to(std::back_inserter(result), "\\x{:02x}", byte);
        } else {
            result += c;
        }
    }
    return result + "\"";
}

std::string layout_rect(const Rect& rect, const Port* port) {
    const int left = port != nullptr ? port->left : 0;
    const int top = port != nullptr ? port->top : 0;
    return std::format("{},{},{},{}", rect.left + left, rect.top + top, rect.right + left, rect.bottom + top);
}

} // namespace sci::vm
