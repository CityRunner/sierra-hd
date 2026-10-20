// SPDX-License-Identifier: GPL-3.0-or-later
//
// Locale-independent ASCII helpers for DOS file names, which game folders
// may hold in either case.

#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace sci::resource::ascii {

[[nodiscard]] constexpr char to_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

[[nodiscard]] constexpr bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] constexpr bool iequals(std::string_view a, std::string_view b) noexcept {
    return std::ranges::equal(a, b, [](char x, char y) { return to_lower(x) == to_lower(y); });
}

[[nodiscard]] constexpr bool istarts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
}

[[nodiscard]] constexpr bool iends_with(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() && iequals(text.substr(text.size() - suffix.size()), suffix);
}

// The character at `index`, or '\0' past the end, as a C string would give.
[[nodiscard]] constexpr char char_at(std::string_view text, std::size_t index) noexcept {
    return index < text.size() ? text[index] : '\0';
}

} // namespace sci::resource::ascii
