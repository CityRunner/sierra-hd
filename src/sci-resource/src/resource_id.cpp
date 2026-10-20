// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/resource/resource.cpp (s_resTypeMapSci0, s_resourceTypeNames,
//       s_resourceTypeSuffixes, ResourceManager::convertResType)
// and translated into this project's style, types and error handling.
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "sci/resource/resource_id.hpp"

#include <array>
#include <cstddef>

namespace sci::resource {

namespace {

struct TypeInfo {
    std::string_view name;
    std::string_view suffix;
};

// Indexed by ResourceType. Names and suffixes as in ScummVM.
constexpr auto type_info = std::to_array<TypeInfo>({
    {.name = "view", .suffix = "v56"},    {.name = "pic", .suffix = "p56"},    {.name = "script", .suffix = "scr"},
    {.name = "text", .suffix = "tex"},    {.name = "sound", .suffix = "snd"},  {.name = "memory", .suffix = ""},
    {.name = "vocab", .suffix = "voc"},   {.name = "font", .suffix = "fon"},   {.name = "cursor", .suffix = "cur"},
    {.name = "patch", .suffix = "pat"},   {.name = "bitmap", .suffix = "bit"}, {.name = "palette", .suffix = "pal"},
    {.name = "cdaudio", .suffix = "cda"}, {.name = "audio", .suffix = "aud"},  {.name = "sync", .suffix = "syn"},
    {.name = "message", .suffix = "msg"}, {.name = "map", .suffix = "map"},    {.name = "heap", .suffix = "hep"},
    {.name = "audio36", .suffix = ""},    {.name = "sync36", .suffix = ""},    {.name = "xlate", .suffix = "trn"},
    {.name = "rave", .suffix = ""},
});
static_assert(type_info.size() == static_cast<std::size_t>(ResourceType::Invalid));

} // namespace

ResourceType resource_type_from_number(std::uint8_t number) noexcept {
    // The SCI0 table maps 0x00-0x15 to the enumerators in order.
    const auto masked = static_cast<std::uint8_t>(number & 0x7FU);
    if (masked >= type_info.size()) {
        return ResourceType::Invalid;
    }
    return static_cast<ResourceType>(masked);
}

ResourceId resource_id_from_word(std::uint16_t word) noexcept {
    return ResourceId{
        .type = resource_type_from_number(static_cast<std::uint8_t>(word >> 11U)),
        .number = static_cast<std::uint16_t>(word & 0x07FFU),
    };
}

std::string_view resource_type_name(ResourceType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < type_info.size() ? type_info[index].name : "invalid";
}

std::string_view resource_type_suffix(ResourceType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < type_info.size() ? type_info[index].suffix : "";
}

} // namespace sci::resource
