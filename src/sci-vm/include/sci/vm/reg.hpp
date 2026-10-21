// SPDX-License-Identifier: GPL-3.0-or-later
//
// The value type of the VM: a register holds a segment and an offset, as in
// ScummVM (reg_t, engines/sci/engine/vm_types.h). Numbers have segment 0;
// pointers name the segment of a script, an object table, a list, the stack
// and so on. Only the SCI0 form is kept: 16-bit offsets.

#pragma once

#include <cstdint>

namespace sci::vm {

using SegmentId = std::uint16_t;

// The segment of temporary variables that were never written. Reading one
// is a script bug that ScummVM's workaround tables resolve.
inline constexpr SegmentId uninitialized_segment = 0x1FFF;

struct Reg {
    SegmentId segment = 0;
    std::uint16_t offset = 0;

    [[nodiscard]] constexpr bool is_null() const noexcept { return segment == 0 && offset == 0; }

    [[nodiscard]] constexpr bool is_number() const noexcept { return segment == 0; }

    [[nodiscard]] constexpr bool is_pointer() const noexcept {
        return segment != 0 && segment != uninitialized_segment;
    }

    [[nodiscard]] constexpr bool is_initialized() const noexcept { return segment != uninitialized_segment; }

    [[nodiscard]] constexpr std::uint16_t to_uint16() const noexcept { return offset; }

    [[nodiscard]] constexpr std::int16_t to_sint16() const noexcept { return static_cast<std::int16_t>(offset); }

    friend constexpr bool operator==(Reg, Reg) noexcept = default;
};

[[nodiscard]] constexpr Reg make_reg(SegmentId segment, int offset) noexcept {
    return Reg{.segment = segment, .offset = static_cast<std::uint16_t>(offset)};
}

[[nodiscard]] constexpr Reg number(int value) noexcept {
    return make_reg(0, value);
}

inline constexpr Reg null_reg{};
inline constexpr Reg true_reg{.segment = 0, .offset = 1};

// Selector numbers, from vocabulary 997.
using Selector = int;
inline constexpr Selector no_selector = -1;

} // namespace sci::vm
