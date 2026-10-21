// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hints in the vector analysis: used where they keep the acceptance
// criteria, left out with a reason otherwise.

#include "pic_builder.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

using sci::test::PicBuilder;
using sci::vector::Plane;

namespace {

constexpr std::uint8_t blue = 1;
constexpr std::uint8_t green = 2;
constexpr std::uint8_t red = 4;

struct Drawn {
    sci::pic::PictureMaps maps;
    sci::pic::DrawTrace trace;
    std::vector<sci::vector::LineRef> lines; // The command of each line, in drawing order.
};

Drawn draw(const std::vector<std::uint8_t>& data) {
    Drawn drawn;
    const auto commands = sci::pic::parse_pic(data);
    SCI_CHECK(commands.has_value());
    if (commands) {
        SCI_CHECK(sci::pic::draw_picture(*commands, drawn.maps, {}, &drawn.trace).has_value());
    }
    for (const auto& primitive : drawn.trace.primitives) {
        if (std::holds_alternative<sci::pic::Line>(primitive.shape)) {
            drawn.lines.push_back(primitive.command);
        }
    }
    return drawn;
}

sci::vector::BuiltScene build(const Drawn& drawn, const sci::vector::PictureHints& hints) {
    const std::array<const sci::vector::PictureHints*, 1> per_picture{&hints};
    return sci::vector::build_scene(drawn.maps, drawn.trace, Plane::Visual, per_picture);
}

sci::vector::ChainHint chain(const char* id, std::vector<sci::vector::LineRef> lines,
                             sci::vector::HintPrimitive primitive) {
    return {.id = id, .lines = std::move(lines), .primitive = primitive, .label = {}, .uncertain = false};
}

} // namespace

SCI_TEST(chain_hints_fit_the_curve_they_ask_for) {
    // Two collinear lines too long for the automatic fitting.
    const Drawn drawn = draw(PicBuilder().color(red).line(20, 100, 60, 101).line(60, 101, 100, 102).end());
    SCI_CHECK(drawn.lines.size() == 2);
    const auto automatic = sci::vector::build_scene(drawn.maps, drawn.trace, Plane::Visual);
    SCI_CHECK(automatic.stats.straightened == 0);
    sci::vector::PictureHints hints;
    hints.chains.push_back(chain("edge", {drawn.lines[0], drawn.lines[1]}, sci::vector::HintPrimitive::Line));
    const auto hinted = build(drawn, hints);
    SCI_CHECK(hinted.stats.hint_chains == 1 && hinted.stats.straightened == 1);
    SCI_CHECK(hinted.stats.rejected_hints.empty());
}

SCI_TEST(chain_hints_must_keep_the_line_pixels) {
    // A bend of one pixel: a straight line within 1 pixel would miss the
    // pixels at the bend.
    const Drawn drawn = draw(PicBuilder().color(red).line(20, 100, 60, 101).line(60, 101, 100, 100).end());
    sci::vector::PictureHints hints;
    hints.chains.push_back(chain("bent", {drawn.lines[0], drawn.lines[1]}, sci::vector::HintPrimitive::Line));
    const auto hinted = build(drawn, hints);
    SCI_CHECK(hinted.stats.hint_chains == 0 && hinted.stats.rejected_hints.size() == 1);
    SCI_CHECK(hinted.stats.rejected_hints[0].id == "bent" &&
              hinted.stats.rejected_hints[0].reason == "the curve misses pixels of its lines");
}

SCI_TEST(hints_for_commands_without_lines_are_rejected) {
    const Drawn drawn = draw(PicBuilder().color(red).line(20, 100, 60, 101).line(60, 101, 100, 102).end());
    sci::vector::PictureHints hints;
    hints.chains.push_back(chain("wrong", {drawn.lines[0], 999}, sci::vector::HintPrimitive::Line));
    const auto hinted = build(drawn, hints);
    SCI_CHECK(hinted.stats.hint_chains == 0 && hinted.stats.rejected_hints.size() == 1);
}

SCI_TEST(gap_hints_close_gaps_that_keep_the_faces) {
    // A real gap of 4 pixels, through which the first fill leaked.
    const Drawn drawn = draw(PicBuilder()
                                 .color(red)
                                 .rectangle(20, 20, 180, 150)
                                 .line(100, 20, 100, 150)
                                 .line(20, 80, 95, 80)
                                 .color(blue)
                                 .fill(50, 50)
                                 .color(green)
                                 .fill(50, 120)
                                 .end());
    SCI_CHECK(drawn.lines.size() == 6);
    sci::vector::PictureHints hints;
    hints.gaps.push_back(sci::vector::GapHint{
        .id = "shelf",
        .from = drawn.lines[5],
        .end = sci::vector::LineEnd::End,
        .to = drawn.lines[4],
        .label = {},
        .uncertain = false,
    });
    const auto hinted = build(drawn, hints);
    SCI_CHECK(hinted.stats.hint_gaps == 1 && hinted.stats.rejected_hints.empty());
    SCI_CHECK(hinted.stats.mixed_faces == 0);
    // Both sides keep the colour the player saw.
    const auto reduced = sci::render::reduce_to_1x(hinted.scene);
    SCI_CHECK(reduced[(50 * 320) + 50] == 0x11 && reduced[(120 * 320) + 50] == 0x11);

    // Too far apart to be a gap.
    const Drawn far = draw(PicBuilder().color(red).line(100, 20, 100, 150).line(20, 80, 85, 80).end());
    hints.gaps[0].from = far.lines[1];
    hints.gaps[0].to = far.lines[0];
    const auto rejected = build(far, hints);
    SCI_CHECK(rejected.stats.hint_gaps == 0 && rejected.stats.rejected_hints.size() == 1);
}
