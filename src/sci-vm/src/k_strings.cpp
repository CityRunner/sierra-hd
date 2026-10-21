// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kstring.cpp (SCI0 functions)
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

#include "kernel_functions.hpp"
#include "machine.hpp"
#include "text_lookup.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>

namespace sci::vm {

namespace {

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// strtol on base 10 from `text[position]`: the value and the characters
// read (0 if no number).
std::pair<long, std::size_t> parse_long(std::string_view text, std::size_t position) {
    std::size_t i = position;
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }
    const std::size_t digits_start = i;
    long value = 0;
    while (i < text.size() && is_digit(text[i])) {
        value = (value * 10) + (text[i] - '0');
        ++i;
    }
    if (i == digits_start) {
        return {0, 0};
    }
    return {negative ? -value : value, i - position};
}

// strcmp and strncmp as glibc computes them (the reference's): the
// difference of the first differing bytes, as unsigned chars. The C library
// only fixes the sign, and kStrCmp passes the value on to the scripts.
int compare_strings(std::string_view first, std::string_view second, std::size_t limit) {
    for (std::size_t i = 0; i < limit; ++i) {
        const int a = i < first.size() ? static_cast<unsigned char>(first[i]) : 0;
        const int b = i < second.size() ? static_cast<unsigned char>(second[i]) : 0;
        if (a != b || a == 0) {
            return a - b;
        }
    }
    return 0;
}

constexpr int align_left = -1;
constexpr int align_none = 0;
constexpr int align_right = 1;
constexpr int align_center = 2;

} // namespace

Reg k_str_end(Machine& machine, std::span<Reg> args) {
    Reg address = args[0];
    address.offset = static_cast<std::uint16_t>(address.offset + machine.segments().strlen(address));
    return address;
}

Reg k_str_cat(Machine& machine, std::span<Reg> args) {
    const SegManager& segments = machine.segments();
    const std::string joined = segments.get_string(args[0]) + segments.get_string(args[1]);
    segments.strcpy(args[0], joined);
    return args[0];
}

Reg k_str_cmp(Machine& machine, std::span<Reg> args) {
    const SegManager& segments = machine.segments();
    const std::string first = segments.get_string(args[0]);
    const std::string second = segments.get_string(args[1]);
    const std::size_t limit = args.size() > 2 ? args[2].to_uint16() : std::string::npos;
    return number(compare_strings(first, second, limit));
}

Reg k_str_cpy(Machine& machine, std::span<Reg> args) {
    const SegManager& segments = machine.segments();
    if (args.size() > 2) {
        const int length = args[2].to_sint16();
        if (length >= 0) {
            segments.strncpy(args[0], args[1], static_cast<std::size_t>(length));
        } else {
            segments.memcpy(args[0], args[1], static_cast<std::size_t>(-length));
        }
    } else {
        segments.strcpy(args[0], args[1]);
    }
    return args[0];
}

Reg k_str_len(Machine& machine, std::span<Reg> args) {
    return number(static_cast<int>(machine.segments().strlen(args[0])));
}

Reg k_str_at(Machine& machine, std::span<Reg> args) {
    if (args[0] == make_reg(0, 0xFFFF)) {
        warn("Attempt to perform kStrAt() on a signal reg");
        return null_reg;
    }
    const SegmentRef ref = machine.segments().dereference(args[0]);
    if (!ref.valid()) {
        warn(std::format("Attempt to StrAt at invalid pointer {:04x}:{:04x}", args[0].segment, args[0].offset));
        return null_reg;
    }
    std::uint16_t offset = args[1].to_uint16();
    std::uint8_t new_value = 0;
    if (args.size() > 2) {
        new_value = static_cast<std::uint8_t>(args[2].to_sint16());
    }
    if (std::cmp_greater_equal(offset, ref.max_size)) {
        warn(std::format("kStrAt offset {:X} exceeds maxSize", offset));
        return machine.acc();
    }
    std::uint8_t value = 0;
    if (ref.is_raw) {
        value = ref.raw[offset];
        if (args.size() > 2) {
            ref.raw[offset] = new_value;
        }
    } else {
        if (ref.skip_byte) {
            ++offset;
        }
        Reg& reg = ref.regs[offset / 2U];
        if ((offset & 1U) == 0) {
            value = static_cast<std::uint8_t>(reg.offset & 0x00FFU);
            if (args.size() > 2) {
                reg.offset = static_cast<std::uint16_t>((reg.offset & 0xFF00U) | new_value);
                reg.segment = 0;
            }
        } else {
            value = static_cast<std::uint8_t>(reg.offset >> 8U);
            if (args.size() > 2) {
                reg.offset =
                    static_cast<std::uint16_t>((reg.offset & 0x00FFU) | static_cast<unsigned>(new_value << 8U));
                reg.segment = 0;
            }
        }
    }
    return number(value);
}

Reg k_read_number(Machine& machine, std::span<Reg> args) {
    const std::string text = machine.segments().get_string(args[0]);
    std::size_t i = 0;
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    std::int16_t result = 0;
    std::int16_t sign = 1;
    if (i < text.size() && text[i] == '-') {
        sign = -1;
        ++i;
    }
    // SSCI's atoi did not clip; compute in 16 bits.
    if (i < text.size() && text[i] == '$') {
        ++i;
        for (; i < text.size(); ++i) {
            const char c = text[i];
            int digit = 0;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = c - 'A' + 10;
            } else {
                break;
            }
            result = static_cast<std::int16_t>((result * 16) + digit);
        }
    } else {
        for (; i < text.size(); ++i) {
            const char c = text[i];
            if (!is_digit(c)) {
                break;
            }
            result = static_cast<std::int16_t>((result * 10) + (c - '0'));
        }
    }
    result = static_cast<std::int16_t>(result * sign);
    return number(result);
}

Reg k_get_far_text(Machine& machine, std::span<Reg> args) {
    const std::string text = lookup_text(machine, number(args[0].to_uint16()), args[1].to_uint16());
    if (args[2].is_null()) {
        (void)machine.segments().allocate_dynmem(text.size() + 1, "Mac FarText", args[2]);
    }
    machine.segments().strcpy(args[2], text);
    return args[2];
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): one formatter, kept close to ScummVM's kFormat.
Reg k_format(Machine& machine, std::span<Reg> args) {
    const Reg dest = args[0];
    const Reg position = args[1];
    std::size_t start = 2;
    if (position.segment == 0) {
        // QFG1 VGA Mac calls this without the destination.
        if (args.size() < 3) {
            return null_reg;
        }
        start = 3;
    }
    const int index = start == 3 ? args[2].to_uint16() : 0;
    const std::string source = lookup_text(machine, position, index);

    // The parameters, copied so that formatting cannot overwrite them.
    const auto parameter = [&](std::size_t i) -> std::uint16_t {
        return start + i < args.size() ? args[start + i].to_uint16() : 0;
    };
    const auto parameter_reg = [&](std::size_t i) -> Reg {
        return start + i < args.size() ? args[start + i] : null_reg;
    };

    std::string target;
    std::size_t param = 0;
    bool in_format = false;
    std::size_t i = 0;
    while (i < source.size()) {
        char xfer = source[i++];
        if (xfer == '%') {
            if (in_format) {
                target += '%';
                in_format = false;
            } else {
                in_format = true;
            }
            continue;
        }
        if (!in_format) {
            target += xfer;
            continue;
        }
        char fill = ' ';
        int align = align_none;
        int length = 0;
        const std::size_t write_start = target.size();
        if (is_digit(xfer) || xfer == '-' || xfer == '=') {
            if (xfer == '0') {
                fill = '0';
            } else if (xfer == '=') {
                align = align_center;
            } else {
                --i; // The length starts here.
            }
            const auto [value, read] = parse_long(source, i);
            length = static_cast<int>(value);
            i += read;
            if (length < 0) {
                align = align_left;
                length = -length;
            } else if (align != align_center) {
                align = align_right;
            }
            xfer = i < source.size() ? source[i++] : '\0';
        }
        switch (xfer) {
        case 's': {
            const Reg reg = parameter_reg(param);
            const std::string text = lookup_text(machine, reg, parameter(param + 1));
            int extra = length - static_cast<int>(text.size());
            extra = std::max(extra, 0);
            param += reg.segment != 0 ? 1 : 2;
            if (align == align_none || align == align_right) {
                target.append(static_cast<std::size_t>(extra), ' ');
            } else if (align == align_center) {
                target.append(static_cast<std::size_t>(extra / 2), ' ');
            }
            target += text;
            if (align == align_center) {
                align = align_none;
                target.append(static_cast<std::size_t>(extra - (extra / 2)), ' ');
            }
            break;
        }
        case 'c': {
            if (align >= 0) {
                while (length-- > 1) {
                    target += ' ';
                }
            }
            const char c = static_cast<char>(parameter(param++));
            if (c != 0) {
                target += c;
            }
            break;
        }
        case 'x':
        case 'u':
        case 'd': {
            int value = parameter(param);
            if (xfer == 'd') {
                value = static_cast<std::int16_t>(value);
            }
            target += xfer == 'x' ? std::format("{:x}", static_cast<unsigned>(value)) : std::format("{}", value);
            ++param;
            break;
        }
        default:
            target += '%';
            target += xfer;
            break;
        }
        in_format = false;
        if (align != align_none) {
            const int written = static_cast<int>(target.size() - write_start);
            const int padding = length - written;
            if (padding > 0) {
                if (align > 0) {
                    target.insert(write_start, static_cast<std::size_t>(padding), fill);
                } else {
                    target.append(static_cast<std::size_t>(padding), ' ');
                }
            }
        }
    }
    machine.segments().strcpy(dest, target);
    return dest;
}

} // namespace sci::vm
