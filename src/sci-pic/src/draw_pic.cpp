// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/picture.cpp (GfxPicture::drawVectorData, default EGA
//       palette and priority tables), engines/sci/graphics/paint16.cpp
//       (GfxPaint16::drawPicture, clearScreen)
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

#include "sci/pic/draw_pic.hpp"

#include "canvas.hpp"
#include "patterns.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <variant>

namespace sci::pic {

namespace {

using core::ErrorCode;
using core::make_error;
using core::Result;
using detail::Canvas;

constexpr std::size_t palette_size = 40;
constexpr std::size_t palette_count = 4;

// vector_defaultEGApalette: each entry is an EGA pair (high nibble: colour
// at odd positions, low nibble: at even positions).
constexpr std::array<std::uint8_t, palette_size> default_ega_palette{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd,
    0xee, 0x88, 0x88, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x88, 0x88, 0xf9, 0xfa, 0xfb,
    0xfc, 0xfd, 0xfe, 0xff, 0x08, 0x19, 0x2a, 0x3b, 0x4c, 0x5d, 0x6e, 0x88,
};

// vector_defaultEGApriority: priority indices map to themselves.
constexpr std::array<std::uint8_t, 16> default_ega_priority{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

class Rasterizer {
public:
    Rasterizer(PictureMaps& maps, const DrawOptions& options)
        : canvas_(maps), palette_base_(options.ega_palette < palette_count ? options.ega_palette * palette_size : 0),
          mirrored_(options.mirrored) {
        for (std::size_t palette = 0; palette < palette_count; ++palette) {
            std::ranges::copy(default_ega_palette,
                              palettes_.begin() + static_cast<std::ptrdiff_t>(palette * palette_size));
        }
    }

    Result<void> operator()(const SetVisual& command) {
        // The index addresses the table from the selected palette onwards;
        // indices past the selected palette read the following palettes.
        const std::size_t entry = palette_base_ + command.index;
        if (entry >= palettes_.size()) {
            return make_error(ErrorCode::InvalidData,
                              std::format("colour index {} is beyond the EGA palette table", command.index));
        }
        const std::uint8_t pair = palettes_[entry];
        color_ = static_cast<std::uint8_t>(pair ^ (pair << 4U));
        return {};
    }

    Result<void> operator()(const DisableVisual& /*command*/) {
        color_ = detail::disabled;
        return {};
    }

    Result<void> operator()(const SetPriority& command) {
        priority_ = default_ega_priority[command.index & 0x0FU];
        return {};
    }

    Result<void> operator()(const DisablePriority& /*command*/) {
        priority_ = detail::disabled;
        return {};
    }

    Result<void> operator()(const SetControl& command) {
        control_ = command.value;
        return {};
    }

    Result<void> operator()(const DisableControl& /*command*/) {
        control_ = detail::disabled;
        return {};
    }

    Result<void> operator()(const Line& command) {
        const Point from = mirror(command.from);
        const Point to = mirror(command.to);
        // The picture port offsets lines by picture_top; ScummVM adds it in
        // 16-bit arithmetic.
        canvas_.draw_line(from.x, static_cast<std::int16_t>(from.y + picture_top), to.x,
                          static_cast<std::int16_t>(to.y + picture_top), color_, priority_, control_);
        return {};
    }

    Result<void> operator()(const Fill& command) {
        const Point at = mirror(command.at);
        if (!detail::flood_fill(canvas_, at.x, at.y, color_, priority_, control_)) {
            return make_error(ErrorCode::InvalidData,
                              std::format("fill starts outside the screen at ({}, {})", at.x, at.y));
        }
        return {};
    }

    Result<void> operator()(const SetPattern& /*command*/) {
        return {}; // The parser records the pattern code with each point.
    }

    Result<void> operator()(const Pattern& command) {
        const Point at = mirror(command.at);
        detail::draw_pattern(canvas_, at.x, at.y, color_, priority_, control_, command.code, command.texture);
        return {};
    }

    Result<void> operator()(const SetPaletteEntries& command) {
        for (const auto& [index, value] : command.entries) {
            palettes_[index] = value;
        }
        return {};
    }

    Result<void> operator()(const SetPalette& command) {
        std::ranges::copy(command.colors,
                          palettes_.begin() + static_cast<std::ptrdiff_t>(command.number * palette_size));
        return {};
    }

    Result<void> operator()(const EmbeddedView& /*command*/) {
        return make_error(ErrorCode::Unsupported, "embedded views in pictures are not supported");
    }

    Result<void> operator()(const SetPriorityTable& command) {
        result_.priority_bands = command.bands;
        return {};
    }

    Result<DrawResult> finish() {
        canvas_.dither();
        return result_;
    }

private:
    // Mirroring reflects x around the screen centre; every point the stream
    // holds is derived from an absolute one, so this matches ScummVM, which
    // mirrors while decoding coordinates.
    [[nodiscard]] Point mirror(Point point) const noexcept {
        if (!mirrored_) {
            return point;
        }
        return Point{.x = static_cast<std::int16_t>(screen_width - 1 - point.x), .y = point.y};
    }

    Canvas canvas_;
    std::array<std::uint8_t, palette_size * palette_count> palettes_{};
    std::size_t palette_base_;
    bool mirrored_;
    std::uint8_t color_ = 0; // EGA pictures start drawing with colour 0.
    std::uint8_t priority_ = detail::disabled;
    std::uint8_t control_ = detail::disabled;
    DrawResult result_;
};

// GfxPaint16::clearScreen on the picture port: white, priority 0, control 0.
void clear_picture_port(PictureMaps& maps) {
    for (int y = picture_top; y < screen_height; ++y) {
        for (int x = 0; x < screen_width; ++x) {
            const std::size_t i = PictureMaps::index(x, y);
            maps.visual[i] = 15;
            maps.priority[i] = 0;
            maps.control[i] = 0;
            maps.pairs[i] = 0xFF;
        }
    }
}

} // namespace

Result<DrawResult> draw_picture(std::span<const PicCommand> commands, PictureMaps& maps, const DrawOptions& options) {
    if (!options.add_to) {
        clear_picture_port(maps);
    }
    Rasterizer rasterizer(maps, options);
    for (const PicCommand& command : commands) {
        if (auto drawn = std::visit(rasterizer, command.operation); !drawn) {
            return std::unexpected(drawn.error());
        }
    }
    return rasterizer.finish();
}

} // namespace sci::pic
