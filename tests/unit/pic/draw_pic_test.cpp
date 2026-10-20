// SPDX-License-Identifier: GPL-3.0-or-later

#include "pic_builder.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using sci::core::ErrorCode;
using sci::pic::PictureMaps;
using sci::test::PicBuilder;

namespace {

// Default EGA palette indices: 4 is solid red (0x44), 17 is the pair
// (blue at even positions, black at odd: 0x01), 15 is solid white.
constexpr std::uint8_t red = 4;
constexpr std::uint8_t blue_black = 17;
constexpr int top = sci::pic::picture_top;

PictureMaps draw(const std::vector<std::uint8_t>& data, const sci::pic::DrawOptions& options = {}) {
    PictureMaps maps;
    const auto commands = sci::pic::parse_pic(data);
    SCI_CHECK(commands.has_value());
    if (commands) {
        SCI_CHECK(sci::pic::draw_picture(*commands, maps, options).has_value());
    }
    return maps;
}

std::uint8_t visual(const PictureMaps& maps, int x, int y) {
    return maps.visual[PictureMaps::index(x, y + top)];
}

std::uint8_t priority(const PictureMaps& maps, int x, int y) {
    return maps.priority[PictureMaps::index(x, y + top)];
}

} // namespace

SCI_TEST(empty_picture_clears_the_picture_port) {
    const PictureMaps maps = draw(PicBuilder().end());
    SCI_CHECK(maps.visual[PictureMaps::index(5, 5)] == 0); // Menu bar rows stay as they were.
    SCI_CHECK(visual(maps, 0, 0) == 15 && visual(maps, 319, 189) == 15);
    SCI_CHECK(priority(maps, 100, 100) == 0);
    SCI_CHECK(maps.pairs[PictureMaps::index(100, 110)] == 0xFF);
}

SCI_TEST(lines_draw_in_the_picture_port) {
    const PictureMaps maps = draw(PicBuilder().color(red).priority(7).line(10, 20, 14, 20).end());
    for (int x = 10; x <= 14; ++x) {
        SCI_CHECK(visual(maps, x, 20) == 4 && priority(maps, x, 20) == 7);
    }
    SCI_CHECK(visual(maps, 15, 20) == 15 && priority(maps, 15, 20) == 0);
}

SCI_TEST(sloped_lines_follow_sierras_bresenham) {
    const PictureMaps maps = draw(PicBuilder().color(red).line(0, 0, 4, 2).end());
    // dx 8, dy 4: the line steps down at x 1 and x 3.
    const std::array<int, 5> expected_y{0, 1, 1, 2, 2};
    for (int x = 0; x <= 4; ++x) {
        SCI_CHECK(visual(maps, x, expected_y[static_cast<std::size_t>(x)]) == 4);
    }
    SCI_CHECK(visual(maps, 1, 0) == 15 && visual(maps, 3, 1) == 15);
}

SCI_TEST(dithered_colours_alternate_and_keep_their_pair) {
    const PictureMaps maps = draw(PicBuilder().color(blue_black).line(10, 20, 13, 20).end());
    // (x ^ y) even shows the low nibble (blue, 1), odd the high one (black, 0).
    SCI_CHECK(visual(maps, 10, 20) == 1 && visual(maps, 11, 20) == 0);
    SCI_CHECK(maps.pairs[PictureMaps::index(11, 20 + top)] == 0x01);
}

SCI_TEST(fills_spread_over_white_inside_outlines) {
    const PictureMaps maps = draw(PicBuilder().color(red).rectangle(10, 10, 20, 20).fill(15, 15).end());
    SCI_CHECK(visual(maps, 15, 15) == 4 && visual(maps, 11, 19) == 4);
    SCI_CHECK(visual(maps, 25, 15) == 15); // Outside stays white.
}

SCI_TEST(fills_leak_through_gaps) {
    // The bottom edge leaves out (11, 20), so the fill escapes through it.
    // (A gap only at a corner is not enough: the fill is 4-connected.)
    PicBuilder pic;
    pic.color(red).line(10, 10, 20, 10).line(20, 10, 20, 20).line(20, 20, 12, 20).line(10, 20, 10, 10);
    const PictureMaps maps = draw(pic.fill(15, 15).end());
    SCI_CHECK(visual(maps, 100, 100) == 4);
}

SCI_TEST(fills_obey_sierras_abort_rules) {
    // White (palette index 31, solid 15) never fills; a fill does not start
    // on a non-white pixel.
    const PictureMaps white = draw(PicBuilder().color(31).fill(50, 50).end());
    SCI_CHECK(visual(white, 50, 50) == 15);
    const PictureMaps on_line = draw(PicBuilder().color(red).line(0, 50, 319, 50).color(2).fill(10, 50).end());
    SCI_CHECK(visual(on_line, 10, 50) == 4 && visual(on_line, 10, 60) == 15);
    // Priority-only fills spread over priority 0.
    const PictureMaps prio = draw(PicBuilder().no_visual().priority(9).fill(10, 10).end());
    SCI_CHECK(priority(prio, 300, 180) == 9 && visual(prio, 300, 180) == 15);
}

SCI_TEST(patterns_draw_boxes_and_circles) {
    // Size 0 rectangle: a 2x1 box at the point.
    const PictureMaps box = draw(PicBuilder().color(red).pattern_code(0x10).pattern(40, 40).end());
    SCI_CHECK(visual(box, 40, 40) == 4 && visual(box, 41, 40) == 4 && visual(box, 42, 40) == 15);
    // Size 1 circle: a 4x3 box whose bitmap is 0x72 0x02 (rows .X.. XXX. .X..).
    const PictureMaps circle = draw(PicBuilder().color(red).pattern_code(0x01).pattern(40, 40).end());
    SCI_CHECK(visual(circle, 40, 39) == 4 && visual(circle, 39, 39) == 15);
    SCI_CHECK(visual(circle, 39, 40) == 4 && visual(circle, 41, 40) == 4 && visual(circle, 42, 40) == 15);
}

SCI_TEST(rectangle_patterns_wrap_at_the_right_edge) {
    // A 2x1 box at x 319: its second column wraps to (0, y + 1) with the
    // dither colours swapped.
    const PictureMaps maps = draw(PicBuilder().color(blue_black).pattern_code(0x10).pattern(319, 40).end());
    SCI_CHECK(maps.pairs[PictureMaps::index(319, 40 + top)] == 0x01);
    SCI_CHECK(maps.pairs[PictureMaps::index(0, 41 + top)] == 0x10);
}

SCI_TEST(mirroring_reflects_x) {
    const PictureMaps maps = draw(PicBuilder().color(red).line(10, 20, 12, 20).end(), {.mirrored = true});
    SCI_CHECK(visual(maps, 309, 20) == 4 && visual(maps, 307, 20) == 4 && visual(maps, 10, 20) == 15);
}

SCI_TEST(ega_palettes_can_be_selected_and_replaced) {
    PicBuilder pic;
    pic.bytes({0xFE, 0x00, 41, 0x22}); // Palette 1, entry 1: solid green.
    const PictureMaps maps = draw(pic.color(1).line(0, 0, 3, 0).end(), {.ega_palette = 1});
    SCI_CHECK(visual(maps, 0, 0) == 2);
}

SCI_TEST(overlays_keep_the_earlier_picture) {
    PictureMaps maps;
    const auto first = sci::pic::parse_pic(PicBuilder().color(red).line(0, 0, 5, 0).end());
    const auto second = sci::pic::parse_pic(PicBuilder().color(2).line(0, 1, 5, 1).end());
    SCI_CHECK(first && second);
    if (first && second) {
        SCI_CHECK(sci::pic::draw_picture(*first, maps, {}).has_value());
        SCI_CHECK(sci::pic::draw_picture(*second, maps, {.add_to = true}).has_value());
        SCI_CHECK(visual(maps, 3, 0) == 4 && visual(maps, 3, 1) == 2);
    }
}

SCI_TEST(priority_tables_are_reported) {
    PicBuilder pic;
    pic.bytes({0xFE, 0x08, 42, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170});
    const auto commands = sci::pic::parse_pic(pic.end());
    PictureMaps maps;
    const auto result = commands ? sci::pic::draw_picture(*commands, maps, {})
                                 : sci::core::Result<sci::pic::DrawResult>(std::unexpected(commands.error()));
    SCI_CHECK(result && result->priority_bands && (*result->priority_bands)[0] == 42);
}

SCI_TEST(drawing_errors) {
    PictureMaps maps;
    const auto off_screen = sci::pic::parse_pic(PicBuilder().color(red).fill(400, 10).end());
    SCI_CHECK(off_screen.has_value());
    if (off_screen) {
        const auto drawn = sci::pic::draw_picture(*off_screen, maps, {});
        SCI_CHECK(!drawn && drawn.error().code == ErrorCode::InvalidData);
    }
    // Colour index 40 of palette 3 would be entry 160, past the table.
    const auto beyond = sci::pic::parse_pic(PicBuilder().color(40).end());
    SCI_CHECK(beyond.has_value());
    if (beyond) {
        SCI_CHECK(sci::pic::draw_picture(*beyond, maps, {.ega_palette = 2}).has_value());
        SCI_CHECK(!sci::pic::draw_picture(*beyond, maps, {.ega_palette = 3}));
    }
}
