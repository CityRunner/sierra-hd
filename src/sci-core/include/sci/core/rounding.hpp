// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rounding helpers with explicitly defined results (spec section 5.3), so
// that no code depends on a platform's rounding mode or on the rounding of
// the C++ conversions.
#pragma once

#include "sci/core/assert.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace sci::core {

// floor(a / b) for b > 0.
[[nodiscard]] constexpr std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept {
    SCI_ASSERT(b > 0);
    const std::int64_t quotient = a / b;
    return (a % b != 0 && a < 0) ? quotient - 1 : quotient;
}

// ceil(a / b) for b > 0.
[[nodiscard]] constexpr std::int64_t ceil_div(std::int64_t a, std::int64_t b) noexcept {
    SCI_ASSERT(b > 0);
    const std::int64_t quotient = a / b;
    return (a % b != 0 && a > 0) ? quotient + 1 : quotient;
}

// num / den rounded to the nearest integer, halves upwards, for den > 0.
[[nodiscard]] constexpr std::uint64_t div_round_half_up(std::uint64_t num, std::uint64_t den) noexcept {
    SCI_ASSERT(den > 0 && num <= (std::numeric_limits<std::uint64_t>::max() - den) / 2);
    return ((2 * num) + den) / (2 * den);
}

// The largest integer not above value. value must be finite and within the
// range of int64_t.
[[nodiscard]] inline std::int64_t floor_to_int(double value) noexcept {
    const double floored = std::floor(value);
    SCI_ASSERT(floored >= -9.2e18 && floored <= 9.2e18);
    return static_cast<std::int64_t>(floored);
}

// The smallest integer not below value, with the same range as floor_to_int.
[[nodiscard]] inline std::int64_t ceil_to_int(double value) noexcept {
    const double ceiled = std::ceil(value);
    SCI_ASSERT(ceiled >= -9.2e18 && ceiled <= 9.2e18);
    return static_cast<std::int64_t>(ceiled);
}

// value rounded to the nearest integer, halves upwards. Exact for |value|
// below 2^52, where value + 0.5 is computed without rounding.
[[nodiscard]] inline std::int64_t round_half_up(double value) noexcept {
    SCI_ASSERT(value > -4.5e15 && value < 4.5e15);
    return static_cast<std::int64_t>(std::floor(value + 0.5));
}

} // namespace sci::core
