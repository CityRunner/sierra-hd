// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parses arbitrary picture data and draws it. The first input byte selects
// the drawing options (mirroring, adding, EGA palette); the rest is the
// picture. Checks that the pair map always explains the visual map.

#include "sci/core/assert.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    if (input.empty()) {
        return 0;
    }
    const auto commands = sci::pic::parse_pic(input.subspan(1));
    if (!commands) {
        return 0;
    }
    const sci::pic::DrawOptions options{
        .mirrored = (input[0] & 1U) != 0,
        .add_to = (input[0] & 2U) != 0,
        .ega_palette = static_cast<std::uint8_t>((unsigned{input[0]} >> 2U) & 3U),
    };
    sci::pic::PictureMaps maps;
    if (!sci::pic::draw_picture(*commands, maps, options)) {
        return 0;
    }
    for (int y = 0; y < sci::pic::screen_height; ++y) {
        for (int x = 0; x < sci::pic::screen_width; ++x) {
            const std::size_t i = sci::pic::PictureMaps::index(x, y);
            SCI_ASSERT(maps.visual[i] == sci::pic::pair_color_at(maps.pairs[i], x, y));
            SCI_ASSERT(maps.priority[i] < 16 && maps.control[i] < 16);
        }
    }
    return 0;
}
