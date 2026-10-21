// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/analysis_cache.hpp"
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
