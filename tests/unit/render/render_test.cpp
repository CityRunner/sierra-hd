// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/executor.hpp"
#include "sci/pic/picture_maps.hpp"
#include "sci/render/colors.hpp"
#include "sci/render/original.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/render/reference.hpp"
#include "sci/vector/scene.hpp"
#include "sci_test.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <variant>
#include <vector>

using sci::render::Rgb8;
using sci::vector::Scene;

namespace {

// A scene of two uniform faces split by a vertical edge at x = split.
Scene split_scene(double split, std::uint8_t left, std::uint8_t right) {
    Scene scene;
    scene.faces = {
        {.fill = sci::vector::FaceFill::Uniform, .value = left},
        {.fill = sci::vector::FaceFill::Uniform, .value = right},
    };
    scene.edges = {
        {.top = {.x = 0, .y = 0}, .bottom = {.x = 0, .y = 190}, .face_after = 0},
        {.top = {.x = split, .y = 0}, .bottom = {.x = split, .y = 190}, .face_after = 1},
    };
    return scene;
}

Rgb8 pixel(const sci::render::RgbaImage& image, int x, int y) {
    const std::size_t i =
        ((static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)) + static_cast<std::size_t>(x)) * 4;
    return {.r = image.pixels[i], .g = image.pixels[i + 1], .b = image.pixels[i + 2]};
}

// Runs the tasks in reverse order, to show that results do not depend on
// the order.
class ReverseExecutor final : public sci::core::Executor {
public:
    void run(std::size_t count, const std::function<void(std::size_t)>& task) override {
        for (std::size_t i = count; i > 0; --i) {
            task(i - 1);
        }
    }
};

} // namespace

SCI_TEST(pairs_blend_as_the_plain_average_of_srgb_values) {
    SCI_CHECK((sci::render::pair_color(0x00) == Rgb8{.r = 0, .g = 0, .b = 0}));
    SCI_CHECK((sci::render::pair_color(0xFF) == Rgb8{.r = 255, .g = 255, .b = 255}));
    SCI_CHECK((sci::render::pair_color(0x0F) == Rgb8{.r = 128, .g = 128, .b = 128})); // 127.5 rounds up.
    SCI_CHECK(sci::render::pair_color(0x0F) == sci::render::pair_color(0xF0));
    SCI_CHECK((sci::render::pair_color(0x11) == Rgb8{.r = 0, .g = 0, .b = 0xAA}));
    SCI_CHECK((sci::render::pair_color(0x14) == Rgb8{.r = 0x55, .g = 0, .b = 0x55}));
    SCI_CHECK(sci::render::to_8bit(0) == 0 && sci::render::to_8bit(65535) == 255 &&
              sci::render::to_8bit(257 * 77) == 77);
}

SCI_TEST(edges_split_pixels_by_coverage) {
    // The edge at x = 100.25 covers a quarter of pixel 100 with the left face.
    const Scene scene = split_scene(100.25, 0x00, 0xFF);
    sci::core::SerialExecutor executor;
    const auto image = sci::render::render_hd(scene, 1, executor);
    SCI_CHECK(image.width == 320 && image.height == 190);
    SCI_CHECK((pixel(image, 99, 50) == Rgb8{.r = 0, .g = 0, .b = 0}));
    SCI_CHECK((pixel(image, 101, 50) == Rgb8{.r = 255, .g = 255, .b = 255}));
    // 4 of 16 sample columns black: 191.25 rounds to 191.
    SCI_CHECK((pixel(image, 100, 50) == Rgb8{.r = 191, .g = 191, .b = 191}));
    const auto scaled = sci::render::render_hd(scene, 2, executor);
    SCI_CHECK(scaled.width == 640 && (pixel(scaled, 200, 7) == Rgb8{.r = 128, .g = 128, .b = 128}));
}

SCI_TEST(layers_paint_over_faces_in_order) {
    Scene scene = split_scene(160, 0x11, 0x11);
    scene.layers.emplace_back(sci::vector::Box{.left = 10, .top = 10, .right = 20, .bottom = 20, .value = 0x44});
    scene.layers.emplace_back(
        sci::vector::Stroke{.points = {{.x = 5.5, .y = 15.5}, {.x = 30.5, .y = 15.5}}, .value = 0x22, .cuts = {}});
    scene.layers.emplace_back(sci::vector::Blocks{.pixels = {(50 * 320) + 50}, .values = {0xFF}});
    const auto reduced = sci::render::reduce_to_1x(scene);
    SCI_CHECK(reduced[(12 * 320) + 12] == 0x44);
    SCI_CHECK(reduced[(15 * 320) + 12] == 0x22 && reduced[(15 * 320) + 25] == 0x22);
    SCI_CHECK(reduced[(16 * 320) + 25] == 0x11 && reduced[(14 * 320) + 25] == 0x11);
    SCI_CHECK(reduced[(50 * 320) + 50] == 0xFF && reduced[(50 * 320) + 51] == 0x11);
}

SCI_TEST(cuts_end_strokes_on_a_straight_edge) {
    Scene scene = split_scene(160, 0x11, 0x11);
    // A horizontal stroke from x = 5.5 to 30.5, cut at x = 20 (and a
    // diagonal cut through its other end).
    scene.layers.emplace_back(sci::vector::Stroke{
        .points = {{.x = 5.5, .y = 15.5}, {.x = 30.5, .y = 15.5}},
        .value = 0x22,
        .cuts =
            {
                {.point = {.x = 20, .y = 0}, .normal = {.x = 1, .y = 0}},
                {.point = {.x = 6, .y = 15}, .normal = {.x = -1, .y = 1}},
            },
    });
    const auto reduced = sci::render::reduce_to_1x(scene);
    SCI_CHECK(reduced[(15 * 320) + 19] == 0x22 && reduced[(15 * 320) + 21] == 0x11);
    SCI_CHECK(reduced[(15 * 320) + 10] == 0x22);
    // The reference evaluator agrees with the rasteriser.
    const sci::vector::Stroke& stroke = std::get<sci::vector::Stroke>(scene.layers.back());
    SCI_CHECK(stroke.kept({.x = 19.9, .y = 15.5}) && !stroke.kept({.x = 20.1, .y = 15.5}));
    SCI_CHECK(sci::render::reference_value(scene, {.x = 19.9, .y = 15.5}) == 0x22);
    SCI_CHECK(sci::render::reference_value(scene, {.x = 20.1, .y = 15.5}) == 0x11);
}

SCI_TEST(fallback_faces_take_block_values) {
    Scene scene = split_scene(160, 0x11, 0x22);
    scene.faces[1].fill = sci::vector::FaceFill::Blocks;
    scene.block_values[(30 * 320) + 200] = 0x44;
    const auto reduced = sci::render::reduce_to_1x(scene);
    SCI_CHECK(reduced[(30 * 320) + 200] == 0x44 && reduced[(30 * 320) + 100] == 0x11);
}

SCI_TEST(results_do_not_depend_on_task_order) {
    Scene scene = split_scene(123.4, 0x13, 0x8F);
    scene.layers.emplace_back(
        sci::vector::Ellipse{.centre = {.x = 80, .y = 60}, .radius_x = 30.3, .radius_y = 21.7, .value = 0x5D});
    sci::core::SerialExecutor serial;
    ReverseExecutor reverse;
    SCI_CHECK(sci::render::render_hd(scene, 3, serial).pixels == sci::render::render_hd(scene, 3, reverse).pixels);
    SCI_CHECK(sci::render::render_priority(scene, 2, serial) == sci::render::render_priority(scene, 2, reverse));
}

SCI_TEST(original_mode_scales_the_visual_map) {
    sci::pic::PictureMaps maps;
    maps.visual[sci::pic::PictureMaps::index(3, sci::pic::picture_top + 2)] = 12;
    const auto frame = sci::render::render_original(maps, 3);
    SCI_CHECK(frame.size() == static_cast<std::size_t>(960) * 570);
    SCI_CHECK(frame[(6 * 960) + 9] == 12 && frame[(8 * 960) + 11] == 12 && frame[(9 * 960) + 9] == 0);
    const auto image = sci::render::indices_to_rgba(frame, 960, 570);
    SCI_CHECK((pixel(image, 10, 7) == Rgb8{.r = 0xFF, .g = 0x55, .b = 0x55}));
}
