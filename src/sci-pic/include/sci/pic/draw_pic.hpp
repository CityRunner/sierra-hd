// SPDX-License-Identifier: GPL-3.0-or-later
//
// The exact 1x rasteriser (path 1, spec section 6.2), ported from ScummVM's
// GfxPicture vector drawing for EGA pictures. Drawing a picture into the
// maps gives, at every pixel, the same visual, priority and control values
// as ScummVM's kDrawPic, compared as palette-index buffers.

#pragma once

#include "sci/core/error.hpp"
#include "sci/pic/pic_command.hpp"
#include "sci/pic/picture_maps.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace sci::pic {

struct DrawOptions {
    bool mirrored = false;        // Draw left-right mirrored.
    bool add_to = false;          // Draw over the maps instead of clearing the picture port first.
    std::uint8_t ega_palette = 0; // Which of the four EGA palettes colour indices refer to (others mean 0).
};

struct DrawResult {
    // The priority bands set by the picture, if it sets them.
    std::optional<std::array<std::uint8_t, 14>> priority_bands;
};

// Draws the commands into the maps. Without add_to, the picture port (rows
// 10-199) is cleared first: visual white (15), priority and control 0. At
// the end the visual map is dithered, as in ScummVM; the pair map keeps the
// colour pair behind every pixel drawn.
//
// Errors: a fill starting outside the screen, a colour index beyond the
// palette table (where ScummVM reads out of bounds) and embedded views (not
// used by SCI0 pictures) leave the maps partly drawn.
[[nodiscard]] core::Result<DrawResult> draw_picture(std::span<const PicCommand> commands, PictureMaps& maps,
                                                    const DrawOptions& options);

} // namespace sci::pic
