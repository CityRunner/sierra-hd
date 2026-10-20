// SPDX-License-Identifier: GPL-3.0-or-later
//
// The optimised scene sampler against the simple reference (spec section
// 5.1), on a picture with curves, dithers, brushes and fallbacks.

#include "pic_builder.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/render/reference.hpp"
#include "sci/vector/build_scene.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using sci::test::PicBuilder;

SCI_TEST(sampler_matches_the_reference) {
    PicBuilder pic;
    pic.color(0).rectangle(10, 10, 300, 180).line(10, 120, 300, 120).line(150, 10, 150, 119);
    const std::array xs{116, 114, 108, 100, 92, 86, 84, 86, 92, 100, 108, 114, 116};
    const std::array ys{96, 103, 108, 110, 108, 103, 96, 89, 84, 82, 84, 89, 96};
    pic.color(12);
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        pic.line(xs[i], ys[i], xs[i + 1], ys[i + 1]);
    }
    pic.color(17).fill(100, 96).color(3).fill(50, 30).color(25).fill(200, 30).color(2).fill(50, 150);
    pic.color(14).pattern_code(0x03).pattern(60, 150).pattern_code(0x12).pattern(80, 150);
    pic.pattern_code(0x24).bytes({0xFA, 0x20}).absolute(250, 150);
    const auto commands = sci::pic::parse_pic(pic.end());
    SCI_CHECK(commands.has_value());
    if (!commands) {
        return;
    }
    sci::pic::PictureMaps maps;
    sci::pic::DrawTrace trace;
    SCI_CHECK(sci::pic::draw_picture(*commands, maps, {}, &trace).has_value());
    const auto built = sci::vector::build_scene(maps, trace, sci::vector::Plane::Visual);
    const sci::render::SceneSampler sampler(built.scene);
    int differences = 0;
    for (const int density : {16, 48}) {
        std::vector<std::uint8_t> values(static_cast<std::size_t>(sci::vector::picture_width * density));
        for (int row = 0; row < sci::vector::picture_height * density; row += 13 * density + 5) {
            const double y = (row + 0.5) / density;
            sampler.sample_row(y, density, values);
            for (std::size_t i = 0; i < values.size(); ++i) {
                const sci::vector::Vec2 p{.x = (static_cast<double>(i) + 0.5) / density, .y = y};
                differences += values[i] == sci::render::reference_value(built.scene, p) ? 0 : 1;
            }
        }
    }
    SCI_CHECK(differences == 0);
}
