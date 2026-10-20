// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/picture.cpp (GfxPicture::draw, drawVectorData,
//       vectorGetAbsCoords, vectorGetRelCoords, vectorGetRelCoordsMed,
//       vectorGetPatternTexture)
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

#include "sci/pic/parse_pic.hpp"

#include <format>
#include <optional>
#include <string_view>

namespace sci::pic {

namespace {

using core::ErrorCode;
using core::make_error;
using core::Result;

enum Opcode : std::uint8_t {
    op_set_color = 0xF0,
    op_disable_visual = 0xF1,
    op_set_priority = 0xF2,
    op_disable_priority = 0xF3,
    op_short_patterns = 0xF4,
    op_medium_lines = 0xF5,
    op_long_lines = 0xF6,
    op_short_lines = 0xF7,
    op_fill = 0xF8,
    op_set_pattern = 0xF9,
    op_absolute_pattern = 0xFA,
    op_set_control = 0xFB,
    op_disable_control = 0xFC,
    op_medium_patterns = 0xFD,
    op_extended = 0xFE,
    op_terminate = 0xFF,
};

enum ExtendedOpcode : std::uint8_t {
    opx_set_palette_entries = 0,
    opx_set_palette = 1,
    opx_mono0 = 2,
    opx_mono1 = 3,
    opx_mono2 = 4,
    opx_mono3 = 5,
    opx_mono4 = 6,
    opx_embedded_view = 7,
    opx_set_priority_table = 8,
};

constexpr std::uint8_t pattern_uses_texture = 0x20;
constexpr std::size_t palette_table_size = 160;
constexpr std::size_t palette_count = 4;

std::unexpected<core::Error> truncated() {
    return make_error(ErrorCode::InvalidData, "picture data ends inside an opcode (no terminator)");
}

// Reads the picture data and appends commands. Every read is checked; a
// read past the end is the error ScummVM reports as an invalid access or a
// missing terminator.
class Parser {
public:
    explicit Parser(std::span<const std::uint8_t> data) : data_(data) {}

    Result<std::vector<PicCommand>> run() {
        while (true) {
            const std::uint32_t start = position();
            const auto op = next();
            if (!op) {
                return make_error(ErrorCode::InvalidData, "picture data has no terminator");
            }
            if (*op == op_terminate) {
                return std::move(commands_);
            }
            if (auto parsed = parse_opcode(*op, start); !parsed) {
                return std::unexpected(parsed.error());
            }
        }
    }

private:
    [[nodiscard]] std::uint32_t position() const noexcept { return static_cast<std::uint32_t>(position_); }

    std::optional<std::uint8_t> next() noexcept {
        if (position_ >= data_.size()) {
            return std::nullopt;
        }
        return data_[position_++];
    }

    // Whether the next byte is a coordinate byte rather than an opcode.
    // Reaching the end here is an error, as the data must end with 0xFF.
    Result<bool> more_coordinates() const {
        if (position_ >= data_.size()) {
            return truncated();
        }
        return data_[position_] < op_set_color;
    }

    Result<std::uint8_t> byte() {
        const auto value = next();
        if (!value) {
            return truncated();
        }
        return *value;
    }

    // vectorGetAbsCoords: x has bits 8-11 in the high nibble of the prefix,
    // y has them in the low nibble.
    Result<Point> absolute() {
        const auto prefix = byte();
        const auto x = byte();
        const auto y = byte();
        if (!prefix || !x || !y) {
            return truncated();
        }
        return Point{
            .x = static_cast<std::int16_t>(*x + ((*prefix & 0xF0U) << 4U)),
            .y = static_cast<std::int16_t>(*y + ((*prefix & 0x0FU) << 8U)),
        };
    }

    // vectorGetRelCoords: one byte, x in the high nibble (bit 7 = negative),
    // y in the low nibble (bit 3 = negative). Arithmetic wraps at 16 bits as
    // in ScummVM.
    Result<Point> relative_short(Point from) {
        const auto delta = byte();
        if (!delta) {
            return truncated();
        }
        const int dx = (*delta & 0x80U) != 0 ? -static_cast<int>((*delta >> 4U) & 7U) : static_cast<int>(*delta >> 4U);
        const int dy = (*delta & 0x08U) != 0 ? -static_cast<int>(*delta & 7U) : static_cast<int>(*delta & 7U);
        return Point{.x = static_cast<std::int16_t>(from.x + dx), .y = static_cast<std::int16_t>(from.y + dy)};
    }

    // vectorGetRelCoordsMed: a y byte (bit 7 = negative, 7-bit magnitude),
    // then an x byte (bit 7 = negative, magnitude 128 - low 7 bits).
    Result<Point> relative_medium(Point from) {
        const auto y_byte = byte();
        const auto x_byte = byte();
        if (!y_byte || !x_byte) {
            return truncated();
        }
        const int dy = (*y_byte & 0x80U) != 0 ? -static_cast<int>(*y_byte & 0x7FU) : static_cast<int>(*y_byte);
        const int dx = (*x_byte & 0x80U) != 0 ? -(128 - static_cast<int>(*x_byte & 0x7FU)) : static_cast<int>(*x_byte);
        return Point{.x = static_cast<std::int16_t>(from.x + dx), .y = static_cast<std::int16_t>(from.y + dy)};
    }

    void emit(std::uint32_t offset, PicOperation operation) {
        commands_.push_back(PicCommand{.offset = offset, .operation = std::move(operation)});
    }

    Result<void> parse_opcode(std::uint8_t op, std::uint32_t start);
    Result<void> parse_lines(std::uint8_t op);
    Result<void> parse_fills();
    Result<void> parse_patterns(std::uint8_t op);
    Result<void> read_texture();
    Result<void> parse_extended(std::uint32_t start);
    Result<void> parse_palette_entries(std::uint32_t start);
    Result<void> parse_palette(std::uint32_t start);
    Result<void> parse_embedded_view(std::uint32_t start);
    Result<void> parse_priority_table(std::uint32_t start);

    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
    std::vector<PicCommand> commands_;
    std::uint8_t pattern_code_ = 0;
    std::uint8_t pattern_texture_ = 0;
};

Result<void> Parser::parse_opcode(std::uint8_t op, std::uint32_t start) {
    switch (op) {
    case op_set_color:
    case op_set_priority:
    case op_set_control:
    case op_set_pattern: {
        const auto value = byte();
        if (!value) {
            return truncated();
        }
        if (op == op_set_color) {
            emit(start, SetVisual{.index = *value});
        } else if (op == op_set_priority) {
            emit(start, SetPriority{.index = static_cast<std::uint8_t>(*value & 0x0FU)});
        } else if (op == op_set_control) {
            emit(start, SetControl{.value = static_cast<std::uint8_t>(*value & 0x0FU)});
        } else {
            pattern_code_ = *value;
            emit(start, SetPattern{.code = *value});
        }
        return {};
    }
    case op_disable_visual:
        emit(start, DisableVisual{});
        return {};
    case op_disable_priority:
        emit(start, DisablePriority{});
        return {};
    case op_disable_control:
        emit(start, DisableControl{});
        return {};
    case op_short_lines:
    case op_medium_lines:
    case op_long_lines:
        return parse_lines(op);
    case op_fill:
        return parse_fills();
    case op_short_patterns:
    case op_medium_patterns:
    case op_absolute_pattern:
        return parse_patterns(op);
    case op_extended:
        return parse_extended(start);
    default:
        return make_error(ErrorCode::InvalidData,
                          std::format("byte {:#04x} at offset {} is not a picture opcode", op, start));
    }
}

// A start point, then one segment per following coordinate group.
Result<void> Parser::parse_lines(std::uint8_t op) {
    auto current = absolute();
    if (!current) {
        return std::unexpected(current.error());
    }
    while (true) {
        const auto more = more_coordinates();
        if (!more) {
            return std::unexpected(more.error());
        }
        if (!*more) {
            return {};
        }
        const std::uint32_t offset = position();
        Result<Point> next_point = op == op_short_lines    ? relative_short(*current)
                                   : op == op_medium_lines ? relative_medium(*current)
                                                           : absolute();
        if (!next_point) {
            return std::unexpected(next_point.error());
        }
        emit(offset, Line{.from = *current, .to = *next_point});
        current = next_point;
    }
}

Result<void> Parser::parse_fills() {
    while (true) {
        const auto more = more_coordinates();
        if (!more) {
            return std::unexpected(more.error());
        }
        if (!*more) {
            return {};
        }
        const std::uint32_t offset = position();
        const auto at = absolute();
        if (!at) {
            return std::unexpected(at.error());
        }
        emit(offset, Fill{.at = *at});
    }
}

// vectorGetPatternTexture: a texture byte precedes each point while the
// pattern code has the texture bit.
Result<void> Parser::read_texture() {
    if ((pattern_code_ & pattern_uses_texture) == 0) {
        return {};
    }
    const auto value = byte();
    if (!value) {
        return truncated();
    }
    pattern_texture_ = static_cast<std::uint8_t>((*value >> 1U) & 0x7FU);
    return {};
}

// Short and medium patterns: an absolute first point, then relative ones;
// absolute patterns: absolute points only.
Result<void> Parser::parse_patterns(std::uint8_t op) {
    std::optional<Point> current;
    while (true) {
        if (current || op == op_absolute_pattern) {
            const auto more = more_coordinates();
            if (!more) {
                return std::unexpected(more.error());
            }
            if (!*more) {
                return {};
            }
        }
        const std::uint32_t offset = position();
        if (auto texture = read_texture(); !texture) {
            return std::unexpected(texture.error());
        }
        Result<Point> at = !current || op == op_absolute_pattern ? absolute()
                           : op == op_short_patterns             ? relative_short(*current)
                                                                 : relative_medium(*current);
        if (!at) {
            return std::unexpected(at.error());
        }
        emit(offset, Pattern{.at = *at, .code = pattern_code_, .texture = pattern_texture_});
        current = *at;
    }
}

Result<void> Parser::parse_extended(std::uint32_t start) {
    const auto opx = byte();
    if (!opx) {
        return truncated();
    }
    switch (*opx) {
    case opx_set_palette_entries:
        return parse_palette_entries(start);
    case opx_set_palette:
        return parse_palette(start);
    case opx_mono0:
        // Monochrome palette data, ignored for EGA (ScummVM skips 41 bytes).
        for (int i = 0; i < 41; ++i) {
            if (!next()) {
                return truncated();
            }
        }
        return {};
    case opx_mono1:
    case opx_mono3:
        return next() ? Result<void>{} : truncated();
    case opx_mono2:
    case opx_mono4:
        return {};
    case opx_embedded_view:
        return parse_embedded_view(start);
    case opx_set_priority_table:
        return parse_priority_table(start);
    default:
        return make_error(ErrorCode::Unsupported,
                          std::format("extended picture opcode {} at offset {} is not supported", *opx, start));
    }
}

Result<void> Parser::parse_palette_entries(std::uint32_t start) {
    SetPaletteEntries command;
    while (true) {
        const auto more = more_coordinates();
        if (!more) {
            return std::unexpected(more.error());
        }
        if (!*more) {
            break;
        }
        const auto index = byte();
        const auto value = byte();
        if (!index || !value) {
            return truncated();
        }
        if (*index >= palette_table_size) {
            return make_error(ErrorCode::InvalidData,
                              std::format("picture writes EGA palette entry {} (of {})", *index, palette_table_size));
        }
        command.entries.emplace_back(*index, *value);
    }
    emit(start, std::move(command));
    return {};
}

Result<void> Parser::parse_palette(std::uint32_t start) {
    const auto number = byte();
    if (!number) {
        return truncated();
    }
    if (*number >= palette_count) {
        return make_error(ErrorCode::InvalidData, std::format("picture sets EGA palette {} (of 4)", *number));
    }
    SetPalette command{.number = *number};
    for (std::uint8_t& color : command.colors) {
        const auto value = byte();
        if (!value) {
            return truncated();
        }
        color = *value;
    }
    emit(start, command);
    return {};
}

Result<void> Parser::parse_embedded_view(std::uint32_t start) {
    const auto at = absolute(); // Never mirrored (vectorGetAbsCoordsNoMirror).
    const auto low = byte();
    const auto high = byte();
    if (!at || !low || !high) {
        return truncated();
    }
    const auto size = static_cast<std::uint16_t>(*low | (*high << 8U));
    const std::uint32_t data_offset = position();
    if (size > data_.size() - position_) {
        return truncated();
    }
    position_ += size;
    emit(start, EmbeddedView{.at = *at, .data_offset = data_offset, .data_size = size});
    return {};
}

Result<void> Parser::parse_priority_table(std::uint32_t start) {
    SetPriorityTable command;
    for (std::uint8_t& band : command.bands) {
        const auto value = byte();
        if (!value) {
            return truncated();
        }
        band = *value;
    }
    emit(start, command);
    return {};
}

} // namespace

Result<std::vector<PicCommand>> parse_pic(std::span<const std::uint8_t> resource) {
    // GfxPicture::draw: a first word of 0x26 marks an SCI1.1 picture.
    if (resource.size() >= 2 && resource[0] == 0x26 && resource[1] == 0x00) {
        return make_error(ErrorCode::Unsupported, "SCI1.1 pictures are not supported");
    }
    return Parser(resource).run();
}

} // namespace sci::pic
