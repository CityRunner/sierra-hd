// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden hashes of scenes rendered from synthetic pictures, without game
// files (spec section 11). CI runs them on x86-64 and ARM64: the same
// hashes on both show that rendering is deterministic across
// architectures. A change of a hash means the output changed; update the
// constants only for an intended change, and say why in the commit.

#include "pic_builder.hpp"
#include "sci/core/executor.hpp"
#include "sci/core/sha256.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/render/original.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>
#include <print>
#include <string>
#include <string_view>
#include <vector>

using sci::test::PicBuilder;

namespace {

// A picture with outlines, gaps closed at 1x, a real leak, dithered fills,
// a curve made of short lines, brushes and priority areas.
std::vector<std::uint8_t> landscape() {
    PicBuilder pic;
    pic.color(0).priority(4).rectangle(10, 10, 300, 180);
    pic.line(10, 120, 300, 120).line(150, 10, 150, 119).line(40, 60, 149, 60);
    pic.line(160, 60, 299, 61); // Ends next to the frame: closed at 1x.
    // A circle of 24 short lines.
    const std::array xs{
        116, 115, 114, 111, 108, 104, 100, 96,  92,  89,  86,  85,  84,
        85,  86,  89,  92,  96,  100, 104, 108, 111, 114, 115, 116,
    };
    const std::array ys{
        96, 100, 103, 106, 108, 110, 110, 110, 108, 106, 103, 100, 96, 92, 89, 86, 84, 82, 82, 82, 84, 86, 89, 92, 96,
    };
    pic.color(12);
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        pic.line(xs[i], ys[i], xs[i + 1], ys[i + 1]);
    }
    pic.color(17).fill(100, 96);                         // Dithered inside the circle.
    pic.color(3).priority(9).fill(50, 30);               // Above the first line.
    pic.color(25).fill(200, 30).color(6).fill(200, 100); // Dithered sky, then the leak below.
    pic.color(2).priority(11).fill(50, 150);
    pic.color(14).pattern_code(0x02).pattern(60, 150).pattern_code(0x13).pattern(80, 150);
    pic.pattern_code(0x24).bytes({0xFA, 0x20}).absolute(250, 150);
    return pic.end();
}

std::string hash(const std::vector<std::uint8_t>& bytes) {
    return sci::core::to_hex(sci::core::sha256(bytes));
}

bool matches(std::string_view name, const std::string& actual, std::string_view expected) {
    if (actual != expected) {
        std::println("{}: {}", name, actual);
        return false;
    }
    return true;
}

} // namespace

SCI_TEST(synthetic_scenes_render_to_their_golden_hashes) {
    for (const bool mirrored : {false, true}) {
        const auto commands = sci::pic::parse_pic(landscape());
        SCI_CHECK(commands.has_value());
        if (!commands) {
            return;
        }
        sci::pic::PictureMaps maps;
        sci::pic::DrawTrace trace;
        SCI_CHECK(sci::pic::draw_picture(*commands, maps, {.mirrored = mirrored}, &trace).has_value());
        const auto visual = sci::vector::build_scene(maps, trace, sci::vector::Plane::Visual);
        const auto priority = sci::vector::build_scene(maps, trace, sci::vector::Plane::Priority);
        sci::core::SerialExecutor executor;
        const std::array<std::string_view, 4> expected =
            mirrored ? std::array<std::string_view, 4>{
                           "93b2068636e68f142f41888017f0fd32c0f76004edf52833edb3fe0bbf8e6878",
                           "faff8ddc5ab4989eb0cc08a9e56f9fefc2b68e30e45b46246efd59ae9dcc7c67",
                           "cc0939f18bfe1678d42c7dd4024697972023f28cb8ef622aa51917b590564e6d",
                           "e7b95d6682afd2cd4bef3466431cb0a79d78f67cad4f756bc45ca4506bd6dabd",
                       }
                     : std::array<std::string_view, 4>{
                           "df85b5c9c0e799b715f78c8dc1f6da330fe87c274515c1cf3c57782933b23bac",
                           "f49b1080e34152712c34741bf413d31e08e0e0bf58ca5f2216ffe5416745cebd",
                           "7adcaef57f3b356416dfdad419b9ca1a9dacbdb9af4d96a202f4b67d64750f06",
                           "8c7cf5b103b4f88d6719efab248f92a659ca7bb7b1eb391e0732ad62ece92c8d",
                       };
        SCI_CHECK(matches("hd1", hash(sci::render::render_hd(visual.scene, 1, executor).pixels), expected[0]));
        SCI_CHECK(matches("hd3", hash(sci::render::render_hd(visual.scene, 3, executor).pixels), expected[1]));
        SCI_CHECK(matches("priority2", hash(sci::render::render_priority(priority.scene, 2, executor)), expected[2]));
        SCI_CHECK(matches("original1", hash(sci::render::render_original(maps, 1)), expected[3]));
    }
}
