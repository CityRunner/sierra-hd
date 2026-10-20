// SPDX-License-Identifier: GPL-3.0-or-later

#include "builders.hpp"
#include "sci/resource/resource_manager.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

using sci::core::ErrorCode;
using sci::resource::GameFile;
using sci::resource::ResourceId;
using sci::resource::ResourceManager;
using sci::resource::ResourceType;
using sci::test::Bytes;

namespace {

constexpr std::array<std::uint8_t, 3> view_data{1, 2, 3};
constexpr std::array<std::uint8_t, 2> script_data{4, 5};
constexpr std::array<std::uint8_t, 1> text_data{6};

constexpr ResourceId view_1{.type = ResourceType::View, .number = 1};
constexpr ResourceId script_52{.type = ResourceType::Script, .number = 52};
constexpr ResourceId text_7{.type = ResourceType::Text, .number = 7};
constexpr ResourceId pic_3{.type = ResourceType::Pic, .number = 3};

// A folder with one volume:
//   view 1 at offset 0, script 52 after it,
//   text 7 whose volume header says text 8,
//   a second map entry for view 1 (ignored: the first one wins),
//   pic 3 pointing past the end of the volume (skipped).
std::vector<GameFile> sample_folder() {
    Bytes volume;
    sci::test::append_stored_entry(volume, ResourceType::View, 1, view_data);
    const auto script_offset = static_cast<std::uint32_t>(volume.size());
    sci::test::append_stored_entry(volume, ResourceType::Script, 52, script_data);
    const auto text_offset = static_cast<std::uint32_t>(volume.size());
    sci::test::append_stored_entry(volume, ResourceType::Text, 8, text_data);

    Bytes map;
    sci::test::append_map_entry(map, ResourceType::View, 1, 1, 0);
    sci::test::append_map_entry(map, ResourceType::Script, 52, 1, script_offset);
    sci::test::append_map_entry(map, ResourceType::Text, 7, 1, text_offset);
    sci::test::append_map_entry(map, ResourceType::View, 1, 1, script_offset);
    sci::test::append_map_entry(map, ResourceType::Pic, 3, 1, 0x10000);
    sci::test::append_map_terminator(map);

    std::vector<GameFile> files;
    files.push_back(GameFile{.name = "resource.map", .bytes = std::move(map)});
    files.push_back(GameFile{.name = "RESOURCE.001", .bytes = std::move(volume)});
    files.push_back(GameFile{.name = "README", .bytes = Bytes{'h', 'i'}});
    return files;
}

bool data_equals(const sci::core::Result<sci::resource::Resource>& resource, std::span<const std::uint8_t> expected) {
    return resource.has_value() && std::ranges::equal(resource->data, expected);
}

} // namespace

SCI_TEST(manager_lists_map_resources_in_order) {
    const auto manager = ResourceManager::open(sample_folder());
    SCI_CHECK(manager.has_value());
    if (manager) {
        const std::vector<ResourceId> expected{view_1, script_52, text_7};
        SCI_CHECK(manager->list() == expected);
        SCI_CHECK(!manager->contains(pic_3));
    }
}

SCI_TEST(manager_loads_from_volumes) {
    const auto manager = ResourceManager::open(sample_folder());
    if (!manager) {
        SCI_CHECK(manager.has_value());
        return;
    }
    const auto view = manager->load(view_1);
    SCI_CHECK(data_equals(view, view_data));
    SCI_CHECK(view && view->source == "RESOURCE.001" && view->loaded_id == view_1);

    // The volume header's id is reported, as ScummVM adopts it on loading.
    const auto text = manager->load(text_7);
    SCI_CHECK(data_equals(text, text_data));
    SCI_CHECK(text && text->loaded_id == ResourceId{.type = ResourceType::Text, .number = 8});

    const auto missing = manager->load(pic_3);
    SCI_CHECK(!missing && missing.error().code == ErrorCode::NotFound);
}

SCI_TEST(manager_applies_patch_files) {
    std::vector<GameFile> files = sample_folder();
    files.push_back(GameFile{.name = "SCRIPT.052", .bytes = Bytes{0x82, 0x00, 9, 9, 9}});
    files.push_back(GameFile{.name = "VIEW.002", .bytes = Bytes{0x80, 0x00, 7}});
    const auto manager = ResourceManager::open(std::move(files));
    if (!manager) {
        SCI_CHECK(manager.has_value());
        return;
    }
    constexpr std::array<std::uint8_t, 3> patched{9, 9, 9};
    const auto script = manager->load(script_52);
    SCI_CHECK(data_equals(script, patched));
    SCI_CHECK(script && script->source == "SCRIPT.052");
    // A patch file may add a resource the map does not have.
    SCI_CHECK(manager->contains(ResourceId{.type = ResourceType::View, .number = 2}));
}

SCI_TEST(manager_requires_map_and_volumes) {
    std::vector<GameFile> no_map = sample_folder();
    no_map.erase(no_map.begin());
    const auto without_map = ResourceManager::open(std::move(no_map));
    SCI_CHECK(!without_map && without_map.error().code == ErrorCode::NotFound);

    std::vector<GameFile> no_volume = sample_folder();
    no_volume.erase(no_volume.begin() + 1);
    const auto without_volume = ResourceManager::open(std::move(no_volume));
    SCI_CHECK(!without_volume && without_volume.error().code == ErrorCode::NotFound);
}
