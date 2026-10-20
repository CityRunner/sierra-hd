// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds the vector scenes of arbitrary picture data and samples them. The
// first input byte selects the drawing options, as in fuzz-pic. Checks
// that every sample of the picture area lies in a face (the scene sampler
// asserts it) and that the priority values stay in range. A few rows are
// sampled, so that each input stays fast.

#include "sci/core/assert.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

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
        .add_to = false,
        .ega_palette = static_cast<std::uint8_t>((unsigned{input[0]} >> 2U) & 3U),
    };
    sci::pic::PictureMaps maps;
    sci::pic::DrawTrace trace;
    if (!sci::pic::draw_picture(*commands, maps, options, &trace)) {
        return 0;
    }
    constexpr int density = 4;
    constexpr std::array rows{0.1, 37.3, 95.5, 140.9, 189.9};
    std::vector<std::uint8_t> values(static_cast<std::size_t>(sci::vector::picture_width) * density);
    for (const auto plane : {sci::vector::Plane::Visual, sci::vector::Plane::Priority}) {
        const auto built = sci::vector::build_scene(maps, trace, plane);
        const sci::render::SceneSampler sampler(built.scene);
        for (const double y : rows) {
            sampler.sample_row(y, density, values);
            for (const std::uint8_t value : values) {
                SCI_ASSERT(plane == sci::vector::Plane::Visual || value < 16);
            }
        }
    }
    return 0;
}
