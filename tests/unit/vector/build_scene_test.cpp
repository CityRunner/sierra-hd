// SPDX-License-Identifier: GPL-3.0-or-later

#include "pic_builder.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

using sci::pic::PictureMaps;
using sci::test::PicBuilder;
using sci::vector::Plane;

namespace {

// Default EGA palette indices.
constexpr std::uint8_t blue = 1;  // Pair 0x11
constexpr std::uint8_t green = 2; // Pair 0x22
constexpr std::uint8_t red = 4;   // Pair 0x44

struct Drawn {
    PictureMaps maps;
    sci::pic::DrawTrace trace;
    sci::vector::BuiltScene built;
};

Drawn build(const std::vector<std::uint8_t>& data, Plane plane = Plane::Visual, sci::pic::DrawOptions options = {}) {
    Drawn drawn;
    const auto commands = sci::pic::parse_pic(data);
    SCI_CHECK(commands.has_value());
    if (commands) {
        SCI_CHECK(sci::pic::draw_picture(*commands, drawn.maps, options, &drawn.trace).has_value());
    }
    drawn.built = sci::vector::build_scene(drawn.maps, drawn.trace, plane);
    return drawn;
}

std::uint8_t map_value(const Drawn& drawn, Plane plane, int x, int y) {
    const std::size_t i = PictureMaps::index(x, y + sci::pic::picture_top);
    return plane == Plane::Visual ? drawn.maps.pairs[i] : drawn.maps.priority[i];
}

// The reduced scene agrees with the 1x map everywhere outside the edge band
// (pixels with a differing neighbour).
bool agrees_outside_band(const Drawn& drawn, Plane plane = Plane::Visual) {
    const std::vector<std::uint8_t> reduced = sci::render::reduce_to_1x(drawn.built.scene);
    for (int y = 0; y < sci::vector::picture_height; ++y) {
        for (int x = 0; x < sci::vector::picture_width; ++x) {
            const std::uint8_t value = map_value(drawn, plane, x, y);
            bool band = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    band = band || (nx >= 0 && nx < sci::vector::picture_width && ny >= 0 &&
                                    ny < sci::vector::picture_height && map_value(drawn, plane, nx, ny) != value);
                }
            }
            if (!band &&
                reduced[(static_cast<std::size_t>(y) * sci::vector::picture_width) + static_cast<std::size_t>(x)] !=
                    value) {
                return false;
            }
        }
    }
    return true;
}

template <typename T>
int count_layers(const sci::vector::Scene& scene) {
    int count = 0;
    for (const auto& layer : scene.layers) {
        count += std::holds_alternative<T>(layer) ? 1 : 0;
    }
    return count;
}

} // namespace

SCI_TEST(closed_outlines_become_uniform_faces) {
    const Drawn drawn = build(
        PicBuilder().color(red).rectangle(50, 50, 150, 120).color(blue).fill(100, 80).color(green).fill(10, 10).end());
    const auto& stats = drawn.built.stats;
    SCI_CHECK(stats.mixed_faces == 0 && stats.lines == 4 && stats.strokes == 4);
    SCI_CHECK(count_layers<sci::vector::Stroke>(drawn.built.scene) == 4);
    SCI_CHECK(agrees_outside_band(drawn));
    const auto reduced = sci::render::reduce_to_1x(drawn.built.scene);
    SCI_CHECK(reduced[(80 * 320) + 100] == 0x11 && reduced[(10 * 320) + 10] == 0x22);
    SCI_CHECK(reduced[(80 * 320) + 50] == 0x44); // The outline.
}

SCI_TEST(gaps_closed_at_1x_are_closed_geometrically) {
    // A line ending next to another one: at 1x their pixels touch, so the
    // fills on either side stay apart; the line is extended to meet it.
    const Drawn drawn = build(PicBuilder()
                                  .color(red)
                                  .rectangle(20, 20, 180, 150)
                                  .line(100, 20, 100, 150)
                                  .line(20, 80, 99, 80)
                                  .color(blue)
                                  .fill(50, 50)
                                  .color(green)
                                  .fill(50, 120)
                                  .end());
    SCI_CHECK(drawn.built.stats.mixed_faces == 0 && drawn.built.stats.extensions >= 1);
    SCI_CHECK(agrees_outside_band(drawn));
}

SCI_TEST(leaks_stay_as_the_player_saw_them) {
    // A real gap: the first fill leaks below the line, the second finds
    // nothing white left.
    const Drawn drawn = build(PicBuilder()
                                  .color(red)
                                  .rectangle(20, 20, 180, 150)
                                  .line(100, 20, 100, 150)
                                  .line(20, 80, 95, 80)
                                  .color(blue)
                                  .fill(50, 50)
                                  .color(green)
                                  .fill(50, 120)
                                  .end());
    SCI_CHECK(drawn.built.stats.mixed_faces == 0);
    SCI_CHECK(agrees_outside_band(drawn));
    const auto reduced = sci::render::reduce_to_1x(drawn.built.scene);
    SCI_CHECK(reduced[(120 * 320) + 50] == 0x11);
}

SCI_TEST(lines_ending_on_the_border_close_the_border) {
    // The picture's edge lies half a pixel beyond the outermost pixel centres.
    const Drawn drawn =
        build(PicBuilder().color(red).line(100, 0, 100, 189).color(blue).fill(50, 50).color(green).fill(150, 50).end());
    SCI_CHECK(drawn.built.stats.mixed_faces == 0);
    SCI_CHECK(agrees_outside_band(drawn));
}

SCI_TEST(dots_bridge_gaps) {
    const Drawn drawn = build(PicBuilder()
                                  .color(red)
                                  .rectangle(20, 20, 180, 150)
                                  .line(20, 80, 99, 80)
                                  .line(100, 81, 100, 81)
                                  .line(101, 82, 180, 82)
                                  .color(blue)
                                  .fill(50, 50)
                                  .color(green)
                                  .fill(50, 120)
                                  .end());
    SCI_CHECK(drawn.built.stats.mixed_faces == 0);
    SCI_CHECK(agrees_outside_band(drawn));
}

SCI_TEST(hidden_lines_are_left_out) {
    const Drawn drawn = build(PicBuilder().color(red).line(10, 10, 30, 10).color(green).line(10, 10, 30, 10).end());
    SCI_CHECK(drawn.built.stats.lines == 2 && drawn.built.stats.hidden_lines == 1 && drawn.built.stats.strokes == 1);
    const auto& stroke = std::get<sci::vector::Stroke>(drawn.built.scene.layers.at(0));
    SCI_CHECK(stroke.value == 0x22 && stroke.points.size() == 2);
    SCI_CHECK(stroke.points[0] == sci::vector::Vec2{.x = 10.5, .y = 10.5});
}

SCI_TEST(brushes_become_shapes_or_blocks) {
    PicBuilder pic;
    pic.color(red).pattern_code(0x02).pattern(60, 60).pattern_code(0x12).pattern(100, 60);
    pic.pattern_code(0x22).bytes({0xFA, 0x0A}).absolute(140, 60);
    const Drawn drawn = build(pic.end());
    SCI_CHECK(drawn.built.stats.brushes == 2 && drawn.built.stats.textured == 1);
    const auto& layers = drawn.built.scene.layers;
    SCI_CHECK(layers.size() == 3);
    if (layers.size() == 3) {
        const auto* circle = std::get_if<sci::vector::Ellipse>(layers.data());
        const auto* box = std::get_if<sci::vector::Box>(&layers[1]);
        const auto* blocks = std::get_if<sci::vector::Blocks>(&layers[2]);
        SCI_CHECK(circle != nullptr && box != nullptr && blocks != nullptr);
        if (circle != nullptr && box != nullptr && blocks != nullptr) {
            // Size 2: a box of 6 x 5 pixels starting at (58, 58).
            SCI_CHECK(circle->centre == sci::vector::Vec2{.x = 61, .y = 60.5});
            SCI_CHECK(circle->radius_x == 3 && circle->radius_y == 2.5);
            SCI_CHECK(box->left == 98 && box->right == 104 && box->top == 58 && box->bottom == 63);
            SCI_CHECK(!blocks->pixels.empty() && blocks->pixels.size() == blocks->values.size());
        }
    }
    SCI_CHECK(agrees_outside_band(drawn));
}

SCI_TEST(priority_scenes_use_the_priority_map) {
    const Drawn drawn =
        build(PicBuilder().no_visual().priority(9).rectangle(40, 40, 120, 100).priority(5).fill(80, 70).end(),
              Plane::Priority);
    SCI_CHECK(drawn.built.scene.plane == Plane::Priority && drawn.built.stats.mixed_faces == 0);
    SCI_CHECK(drawn.built.stats.strokes == 4);
    SCI_CHECK(agrees_outside_band(drawn, Plane::Priority));
    const auto reduced = sci::render::reduce_to_1x(drawn.built.scene);
    SCI_CHECK(reduced[(70 * 320) + 80] == 5 && reduced[(40 * 320) + 80] == 9 && reduced[(10 * 320) + 10] == 0);
}

SCI_TEST(mirrored_pictures_agree_with_their_maps) {
    const Drawn drawn = build(PicBuilder()
                                  .color(red)
                                  .rectangle(20, 20, 180, 150)
                                  .line(100, 20, 100, 150)
                                  .line(20, 80, 99, 80)
                                  .color(blue)
                                  .fill(50, 50)
                                  .color(green)
                                  .fill(50, 120)
                                  .end(),
                              Plane::Visual, {.mirrored = true});
    SCI_CHECK(drawn.built.stats.mixed_faces == 0);
    SCI_CHECK(agrees_outside_band(drawn));
}

SCI_TEST(every_point_of_the_picture_has_a_face) {
    const Drawn drawn = build(PicBuilder().color(red).line(0, 0, 319, 189).line(319, 0, 0, 189).fill(10, 100).end());
    const sci::render::SceneSampler sampler(drawn.built.scene);
    std::vector<std::int32_t> faces(std::size_t{320} * 4);
    for (int row = 0; row < 190 * 4; ++row) {
        sampler.sample_faces((row + 0.5) / 4, 4, faces);
        for (const std::int32_t face : faces) {
            SCI_CHECK(face >= 0 && static_cast<std::size_t>(face) < drawn.built.scene.faces.size());
        }
    }
}

SCI_TEST(curves_drawn_as_short_lines_are_fitted) {
    // A circle of 24 short lines, filled inside.
    PicBuilder pic;
    pic.color(red);
    const std::array xs{
        116, 115, 114, 111, 108, 104, 100, 96,  92,  89,  86,  85,  84,
        85,  86,  89,  92,  96,  100, 104, 108, 111, 114, 115, 116,
    };
    const std::array ys{
        66, 70, 73, 76, 78, 80, 80, 80, 78, 76, 73, 70, 66, 62, 59, 56, 54, 52, 52, 52, 54, 56, 59, 62, 66,
    };
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        pic.line(xs[i], ys[i], xs[i + 1], ys[i + 1]);
    }
    pic.color(blue).fill(100, 66);
    const Drawn drawn = build(pic.end());
    const auto& stats = drawn.built.stats;
    SCI_CHECK(stats.arcs + stats.splines + stats.ellipses > 0 && stats.fitted_lines >= 10);
    SCI_CHECK(stats.mixed_faces == 0);
    SCI_CHECK(agrees_outside_band(drawn));
    // Every line pixel is still touched by its stroke.
    const sci::render::SceneSampler sampler(drawn.built.scene);
    std::vector<std::int32_t> sources(std::size_t{320} * 16);
    std::vector<int> touched(sci::vector::picture_pixels, 0);
    for (int row = 40 * 16; row < 90 * 16; ++row) {
        sampler.sample_sources((row + 0.5) / 16, 16, sources);
        for (std::size_t i = 0; i < sources.size(); ++i) {
            if (sources[i] >= 0) {
                ++touched[(static_cast<std::size_t>(row / 16) * 320) + (i / 16)];
            }
        }
    }
    for (int y = 40; y < 90; ++y) {
        for (int x = 70; x < 130; ++x) {
            const auto owner = drawn.trace.visual_owner[PictureMaps::index(x, y + sci::pic::picture_top)];
            if (owner >= 0 &&
                std::holds_alternative<sci::pic::Line>(drawn.trace.primitives[static_cast<std::size_t>(owner)].shape)) {
                SCI_CHECK(touched[(static_cast<std::size_t>(y) * 320) + static_cast<std::size_t>(x)] > 0);
            }
        }
    }
}
