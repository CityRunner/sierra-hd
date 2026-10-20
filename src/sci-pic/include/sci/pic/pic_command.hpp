// SPDX-License-Identifier: GPL-3.0-or-later
//
// The command stream of an SCI0 picture (PIC) resource: the vector opcodes
// decoded into one command per drawing step. Coordinates are the picture's
// own, as stored (before mirroring and before the picture-port offset).
// Hints (spec section 6.7) refer to commands by their index in the stream.

#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace sci::pic {

struct Point {
    std::int16_t x = 0;
    std::int16_t y = 0;

    friend bool operator==(const Point&, const Point&) = default;
};

// Opcode 0xF0: the visual colour, as an index into the EGA palette table.
struct SetVisual {
    std::uint8_t index = 0;
};

// Opcode 0xF1.
struct DisableVisual {};

// Opcode 0xF2: the priority, as an index (0-15) into the priority table.
struct SetPriority {
    std::uint8_t index = 0;
};

// Opcode 0xF3.
struct DisablePriority {};

// Opcode 0xFB: the control value (0-15).
struct SetControl {
    std::uint8_t value = 0;
};

// Opcode 0xFC.
struct DisableControl {};

// One segment of opcodes 0xF5 to 0xF7 (medium, long and short lines).
struct Line {
    Point from;
    Point to;
};

// One point of opcode 0xF8.
struct Fill {
    Point at;
};

// Opcode 0xF9: pen size (bits 0-2), rectangle (bit 4), textured (bit 5).
struct SetPattern {
    std::uint8_t code = 0;
};

// One point of opcodes 0xF4, 0xFA and 0xFD, with the pattern code in effect
// and the texture number (0-127; meaningful when the code has bit 5 set).
struct Pattern {
    Point at;
    std::uint8_t code = 0;
    std::uint8_t texture = 0;
};

// Extended opcode 0: set single entries of the EGA palette table (160
// entries: four palettes of 40).
struct SetPaletteEntries {
    std::vector<std::pair<std::uint8_t, std::uint8_t>> entries; // (index, value)
};

// Extended opcode 1: replace one of the four EGA palettes.
struct SetPalette {
    std::uint8_t number = 0;
    std::array<std::uint8_t, 40> colors{};
};

// Extended opcode 7: a view cel embedded in the picture. Not used by SCI0
// pictures of the tested release; kept so that the stream is complete.
struct EmbeddedView {
    Point at;
    std::uint32_t data_offset = 0; // Offset of the cel data in the resource.
    std::uint16_t data_size = 0;
};

// Extended opcode 8: the 14 priority band boundaries.
struct SetPriorityTable {
    std::array<std::uint8_t, 14> bands{};
};

using PicOperation =
    std::variant<SetVisual, DisableVisual, SetPriority, DisablePriority, SetControl, DisableControl, Line, Fill,
                 SetPattern, Pattern, SetPaletteEntries, SetPalette, EmbeddedView, SetPriorityTable>;

struct PicCommand {
    std::uint32_t offset = 0; // Of the opcode (or coordinate group) in the resource.
    PicOperation operation;
};

} // namespace sci::pic
