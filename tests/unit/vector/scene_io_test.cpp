// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vector/scene_io.hpp"
#include "sci_test.hpp"

#include <cstdint>
#include <vector>

using sci::vector::Blocks;
using sci::vector::Box;
using sci::vector::Cut;
using sci::vector::Ellipse;
using sci::vector::Face;
using sci::vector::FaceFill;
using sci::vector::Plane;
using sci::vector::read_scene;
using sci::vector::ScanEdge;
using sci::vector::Scene;
using sci::vector::Stroke;
using sci::vector::Vec2;
using sci::vector::write_scene;

namespace {

bool same(const Scene& a, const Scene& b) {
    if (a.plane != b.plane || a.faces.size() != b.faces.size() || a.edges.size() != b.edges.size() ||
        a.block_values != b.block_values || a.layers.size() != b.layers.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.faces.size(); ++i) {
        if (a.faces[i].fill != b.faces[i].fill || a.faces[i].value != b.faces[i].value) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.edges.size(); ++i) {
        if (a.edges[i].top != b.edges[i].top || a.edges[i].bottom != b.edges[i].bottom ||
            a.edges[i].face_after != b.edges[i].face_after) {
            return false;
        }
    }
    // Encoding is a function of the content, so equal bytes mean equal layers.
    return write_scene(a) == write_scene(b);
}

// One of everything, with values that do not survive a rounding to float.
Scene sample() {
    Scene scene;
    scene.plane = Plane::Priority;
    scene.faces = {Face{.fill = FaceFill::Uniform, .value = 3}, Face{.fill = FaceFill::Blocks, .value = 0}};
    scene.edges = {
        ScanEdge{.top = {.x = 0, .y = 0}, .bottom = {.x = 0, .y = 190}, .face_after = 0},
        ScanEdge{.top = {.x = 100.1, .y = 0.3}, .bottom = {.x = 99.9999999999, .y = 150.25}, .face_after = 1},
    };
    scene.block_values[12345] = 7;
    Stroke stroke{
        .source = 4,
        .points = {Vec2{.x = 1.0 / 3.0, .y = 2.5}, Vec2{.x = 7.75, .y = 9.125}},
        .radius = 0.5,
        .value = 0x4C,
        .fitted = true,
        .cuts = {Cut{.point = {.x = 7, .y = 9}, .normal = {.x = 0.6, .y = -0.8}}},
    };
    scene.layers.emplace_back(stroke);
    scene.layers.emplace_back(
        Ellipse{.source = 5, .centre = {.x = 50.5, .y = 60.5}, .radius_x = 3, .radius_y = 2, .value = 9});
    scene.layers.emplace_back(Box{.source = -1, .left = 1, .top = 2, .right = 3, .bottom = 4, .value = 1});
    scene.layers.emplace_back(Blocks{.source = 8, .pixels = {5, 6, 400}, .values = {1, 2, 3}});
    return scene;
}

} // namespace

SCI_TEST(scene_round_trip_is_exact) {
    const Scene scene = sample();
    const auto read = read_scene(write_scene(scene));
    SCI_CHECK(read.has_value());
    SCI_CHECK(read && same(*read, scene));
    const auto empty = read_scene(write_scene(Scene{}));
    SCI_CHECK(empty && same(*empty, Scene{}));
}

SCI_TEST(scene_reader_rejects_damaged_bytes) {
    const std::vector<std::uint8_t> bytes = write_scene(sample());
    // Every truncation fails.
    for (std::size_t size = 0; size < bytes.size(); size += 997) {
        SCI_CHECK(!read_scene(std::span(bytes).first(size)).has_value());
    }
    std::vector<std::uint8_t> longer = bytes;
    longer.push_back(0);
    SCI_CHECK(!read_scene(longer).has_value());
    std::vector<std::uint8_t> version = bytes;
    version[4] = 99;
    SCI_CHECK(!read_scene(version).has_value());
}

SCI_TEST(scene_reader_rejects_inconsistent_scenes) {
    Scene bad_face = sample();
    bad_face.edges[1].face_after = 2;
    SCI_CHECK(!read_scene(write_scene(bad_face)).has_value());
    Scene bad_pixel = sample();
    std::get<Blocks>(bad_pixel.layers[3]).pixels = {5, 4, 400};
    SCI_CHECK(!read_scene(write_scene(bad_pixel)).has_value());
    Scene outside = sample();
    std::get<Blocks>(outside.layers[3]).pixels = {5, 6, 320 * 190};
    SCI_CHECK(!read_scene(write_scene(outside)).has_value());
}
