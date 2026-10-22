// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/sha256.hpp"
#include "sci/platform/analysis_cache.hpp"
#include "sci/vector/build_scene.hpp"
#include "sci/vector/scene_io.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <vector>

using sci::core::Sha256Digest;
using sci::platform::decode_entry;
using sci::platform::encode_entry;
using sci::platform::entry_file_name;
using sci::platform::entry_key;
using sci::platform::PictureOverlay;
using sci::platform::SceneVariant;
using sci::vector::Plane;

namespace {

Sha256Digest digest(std::uint8_t first) {
    Sha256Digest d{};
    d[0] = first;
    return d;
}

} // namespace

SCI_TEST(entry_key_changes_with_every_input) {
    const std::array<std::uint8_t, 3> hint{1, 2, 3};
    const std::array<std::uint8_t, 3> other_hint{1, 2, 4};
    const SceneVariant base{.picture = 44};
    const std::set<Sha256Digest> keys{
        entry_key(digest(1), hint, base),
        entry_key(digest(2), hint, base),
        entry_key(digest(1), other_hint, base),
        entry_key(digest(1), {}, base),
        entry_key(digest(1), hint, SceneVariant{.picture = 45}),
        entry_key(digest(1), hint, SceneVariant{.picture = 44, .plane = Plane::Priority}),
        entry_key(digest(1), hint, SceneVariant{.picture = 44, .mirrored = true}),
        entry_key(digest(1), hint, SceneVariant{.picture = 44, .ega_palette = 2}),
    };
    SCI_CHECK(keys.size() == 8);
    SCI_CHECK(entry_key(digest(1), hint, base) == entry_key(digest(1), hint, base));
}

SCI_TEST(entry_round_trips_and_stale_entries_are_refused) {
    sci::vector::Scene scene;
    scene.faces.push_back(sci::vector::Face{.fill = sci::vector::FaceFill::Uniform, .value = 5});
    const Sha256Digest key = entry_key(digest(7), {}, SceneVariant{.picture = 3});
    const std::vector<std::uint8_t> bytes = encode_entry(key, scene);
    const auto read = decode_entry(bytes, key);
    SCI_CHECK(read && read->faces.size() == 1 && read->faces[0].value == 5);
    SCI_CHECK(!decode_entry(bytes, entry_key(digest(8), {}, SceneVariant{.picture = 3})).has_value());
    SCI_CHECK(!decode_entry(std::span(bytes).first(20), key).has_value());
    std::vector<std::uint8_t> other_version = bytes;
    other_version[4] = 2;
    SCI_CHECK(!decode_entry(other_version, key).has_value());
}

SCI_TEST(entry_file_names_tell_the_variants_apart) {
    SCI_CHECK(entry_file_name(SceneVariant{.picture = 44}) == "pic.044.visual.scene");
    SCI_CHECK(
        entry_file_name(SceneVariant{.picture = 5, .plane = Plane::Priority, .mirrored = true, .ega_palette = 2}) ==
        "pic.005.priority.mirrored.palette2.scene");
}

SCI_TEST(overlays_are_variants_of_their_own) {
    const std::array<std::uint8_t, 3> hint{1, 2, 3};
    const SceneVariant base{.picture = 174};
    const SceneVariant stack{.picture = 174, .overlays = {PictureOverlay{.picture = 74}}};
    const std::vector<std::vector<std::uint8_t>> overlay_hints{{9}};
    const std::set<Sha256Digest> keys{
        entry_key(digest(1), hint, base),
        entry_key(digest(1), hint, stack),
        entry_key(digest(1), hint, stack, overlay_hints),
        entry_key(digest(1), hint, SceneVariant{.picture = 174, .overlays = {PictureOverlay{.picture = 75}}}),
        entry_key(digest(1), hint,
                  SceneVariant{.picture = 174, .overlays = {PictureOverlay{.picture = 74, .mirrored = true}}}),
        entry_key(digest(1), hint,
                  SceneVariant{.picture = 174, .overlays = {PictureOverlay{.picture = 74, .ega_palette = 1}}}),
        entry_key(
            digest(1), hint,
            SceneVariant{.picture = 174, .overlays = {PictureOverlay{.picture = 74}, PictureOverlay{.picture = 74}}}),
    };
    SCI_CHECK(keys.size() == 7);
    SCI_CHECK(entry_file_name(stack) == "pic.174.visual.over.074.scene");
    SCI_CHECK(entry_file_name(SceneVariant{.picture = 1,
                                           .mirrored = true,
                                           .overlays = {PictureOverlay{.picture = 2, .ega_palette = 3},
                                                        PictureOverlay{.picture = 3, .mirrored = true}}}) ==
              "pic.001.visual.mirrored.over.002.palette3.over.003.mirrored.scene");
}

SCI_TEST(keys_without_overlays_are_those_of_the_first_format) {
    // The key as the first cache format computed it.
    const std::array<std::uint8_t, 3> hint{1, 2, 3};
    sci::core::Sha256 hash;
    hash.update(digest(4));
    const auto add = [&](std::uint32_t value) {
        const std::array<std::uint8_t, 4> bytes{
            static_cast<std::uint8_t>(value & 0xFFU),
            static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
            static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
            static_cast<std::uint8_t>(value >> 24U),
        };
        hash.update(bytes);
    };
    add(sci::vector::analyser_version);
    add(sci::vector::scene_format_version);
    add(3);
    hash.update(hint);
    add(44); // Picture, priority plane, mirrored, palette 2.
    add(1);
    add(1);
    add(2);
    SCI_CHECK(entry_key(digest(4), hint,
                        SceneVariant{.picture = 44, .plane = Plane::Priority, .mirrored = true, .ega_palette = 2}) ==
              hash.finish());
}
