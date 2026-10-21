// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <bit>
#include <cstdint>

namespace sci::vm {

// ScummVM computes kSqrt, kGetDistance and kSetJump partly in binary32. The project uses
// only double (spec 5.3), so results are rounded to binary32 here: rounding
// the double result of +, * or sqrt again to 24 bits gives the binary32
// result, as 53 >= 2 * 24 + 2. Only for values in the normal binary32
// range, which these are.
inline double to_binary32(double value) {
    if (value == 0.0) {
        return value;
    }
    constexpr std::uint64_t dropped = 29; // 52 - 23 fraction bits.
    constexpr std::uint64_t mask = (std::uint64_t{1} << dropped) - 1;
    constexpr std::uint64_t half = std::uint64_t{1} << (dropped - 1);
    auto bits = std::bit_cast<std::uint64_t>(value);
    const std::uint64_t lower = bits & mask;
    bits &= ~mask;
    if (lower > half || (lower == half && (bits & (mask + 1)) != 0)) {
        bits += mask + 1; // A carry into the exponent is right.
    }
    return std::bit_cast<double>(bits);
}

} // namespace sci::vm
