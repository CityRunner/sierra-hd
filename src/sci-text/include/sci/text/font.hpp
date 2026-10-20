// SPDX-License-Identifier: GPL-3.0-or-later
//
// SCI fonts: one bitmap glyph per character code, one bit per pixel, most
// significant bit first, rows padded to whole bytes. Decoding follows
// ScummVM's GfxFontFromResource.

#pragma once

#include "sci/core/error.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::text {

struct Glyph {
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::vector<std::uint8_t> pixels; // width * height values, 1 where the glyph is set.
};

struct Font {
    std::uint8_t line_height = 0;
    std::vector<Glyph> glyphs; // Indexed by character code.
};

// Decodes a font resource. A character whose header lies outside the
// resource gets an empty glyph, as in ScummVM; bitmap data outside the
// resource is InvalidData.
[[nodiscard]] core::Result<Font> decode_font(std::span<const std::uint8_t> resource);

} // namespace sci::text
