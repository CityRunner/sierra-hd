// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kmath.cpp (SCI0 functions)
// and translated into this project's style, types and error handling.
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "binary32.hpp"
#include "kernel_functions.hpp"
#include "machine.hpp"
#include "subsystems.hpp"
#include "timing.hpp"
#include "trigonometry.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace sci::vm {

Reg k_random(Machine& machine, std::span<Reg> args) {
    RandomSource& random = machine.subsystems().random();
    if (args.size() == 2) {
        // Unsigned: some games ask for 5-digit numbers.
        const std::uint16_t from = args[0].to_uint16();
        const std::uint16_t to = args[1].to_uint16();
        // A reversed range returns its upper end (Longbow, room 710).
        if (from > to) {
            return number(to);
        }
        auto range = static_cast<std::uint16_t>(to - from + 1);
        // Computed as SSCI did, which Hoyle 4 relies on (kRandom(0, -1)).
        if (range != 0) {
            --range;
        }
        return number(from + static_cast<int>(random.next(range)));
    }
    warn("kRandom: caller requested to get the RNG seed");
    return number(static_cast<int>(random.seed() & 0xFFFFU));
}

Reg k_abs(Machine& /*machine*/, std::span<Reg> args) {
    return number(std::abs(static_cast<int>(args[0].to_sint16())));
}

Reg k_sqrt(Machine& /*machine*/, std::span<Reg> args) {
    const double value = std::abs(static_cast<int>(args[0].to_sint16())); // Exact in binary32.
    return number(static_cast<std::int16_t>(to_binary32(std::sqrt(value))));
}

Reg k_get_angle(Machine& /*machine*/, std::span<Reg> args) {
    // The SCI0 angle: in grads, merged into "degrees" of one or two grads.
    const std::int16_t x1 = args[0].to_sint16();
    const std::int16_t y1 = args[1].to_sint16();
    const std::int16_t x2 = args[2].to_sint16();
    const std::int16_t y2 = args[3].to_sint16();
    auto x_rel = static_cast<std::int16_t>(x2 - x1);
    auto y_rel = static_cast<std::int16_t>(y1 - y2); // The y axis is mirrored.
    if (y1 < y2) {
        y_rel = static_cast<std::int16_t>(-y_rel);
    }
    if (x2 < x1) {
        x_rel = static_cast<std::int16_t>(-x_rel);
    }
    if (y_rel == 0 && x_rel == 0) {
        return number(0);
    }
    auto angle = static_cast<std::int16_t>(100 * x_rel / (x_rel + y_rel));
    if (y1 < y2) {
        angle = static_cast<std::int16_t>(200 - angle);
    }
    if (x2 < x1) {
        angle = static_cast<std::int16_t>(400 - angle);
    }
    angle = static_cast<std::int16_t>(angle - ((angle + 9) / 10));
    return number(static_cast<std::uint16_t>(angle));
}

Reg k_get_distance(Machine& /*machine*/, std::span<Reg> args) {
    const int x_diff = args.size() > 3 ? args[3].to_sint16() : 0;
    const int y_diff = args.size() > 2 ? args[2].to_sint16() : 0;
    const int angle = args.size() > 5 ? args[5].to_sint16() : 0;
    // The difference of two int16 values is exact in binary32.
    const auto x_rel = static_cast<int>(static_cast<double>(args[1].to_sint16() - x_diff) / cos_degrees(angle));
    const int y_rel = args[0].to_sint16() - y_diff;
    const double x_square = to_binary32(to_binary32(x_rel) * to_binary32(x_rel));
    const double sum = to_binary32(x_square + to_binary32(y_rel * y_rel));
    return number(static_cast<std::int16_t>(to_binary32(std::sqrt(sum))));
}

Reg k_times_sin(Machine& /*machine*/, std::span<Reg> args) {
    const int angle = args[0].to_sint16();
    const int factor = args[1].to_sint16();
    return number(static_cast<std::int16_t>(factor * sin_degrees(angle)));
}

Reg k_times_cos(Machine& /*machine*/, std::span<Reg> args) {
    const int angle = args[0].to_sint16();
    const int factor = args[1].to_sint16();
    return number(static_cast<std::int16_t>(factor * cos_degrees(angle)));
}

Reg k_cos_div(Machine& machine, std::span<Reg> args) {
    const int angle = args[0].to_sint16();
    const int value = args[1].to_sint16();
    const double cos_value = cos_degrees(angle);
    if (cos_value < 0.0001 && cos_value > -0.0001) {
        machine.faults().raise("kCosDiv: Attempted division by zero");
        return number(0xFFFF);
    }
    return number(static_cast<std::int16_t>(value / cos_value));
}

Reg k_sin_div(Machine& machine, std::span<Reg> args) {
    const int angle = args[0].to_sint16();
    const int value = args[1].to_sint16();
    const double sin_value = sin_degrees(angle);
    if (sin_value < 0.0001 && sin_value > -0.0001) {
        machine.faults().raise("kSinDiv: Attempted division by zero");
        return number(0xFFFF);
    }
    return number(static_cast<std::int16_t>(value / sin_value));
}

} // namespace sci::vm
