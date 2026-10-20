// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resource types and ids of SCI0. The type numbers, names and patch-file
// suffixes follow ScummVM (engines/sci/resource/resource.h and resource.cpp),
// so that resource dumps and patch-file names match the reference.

#pragma once

#include <compare>
#include <cstdint>
#include <string_view>

namespace sci::resource {

// The types an SCI0 resource map or patch file can name, in ScummVM's order
// (the order of ResourceId comparisons and of the patch-file scan).
enum class ResourceType : std::int8_t {
    View,
    Pic,
    Script,
    Text,
    Sound,
    Memory,
    Vocab,
    Font,
    Cursor,
    Patch,
    Bitmap,
    Palette,
    CdAudio,
    Audio,
    Sync,
    Message,
    Map,
    Heap,
    Audio36,
    Sync36,
    Translation,
    Rave,
    Invalid,
};

// The type for a type number from a resource map, volume header or patch
// file (ScummVM's convertResType for SCI0). The top bit is ignored; numbers
// beyond the SCI0 table give Invalid.
[[nodiscard]] ResourceType resource_type_from_number(std::uint8_t number) noexcept;

// "view", "pic", ...: the names ScummVM uses in patch files and dumps.
[[nodiscard]] std::string_view resource_type_name(ResourceType type) noexcept;

// "v56", "p56", ...: the suffixes of SCI1-style patch file names; empty for
// types without one.
[[nodiscard]] std::string_view resource_type_suffix(ResourceType type) noexcept;

struct ResourceId {
    ResourceType type = ResourceType::Invalid;
    std::uint16_t number = 0;

    friend auto operator<=>(const ResourceId&, const ResourceId&) = default;
};

// The id in the 16-bit form of SCI0 maps and volume headers: the type number
// in the top 5 bits, the resource number in the low 11.
[[nodiscard]] ResourceId resource_id_from_word(std::uint16_t word) noexcept;

} // namespace sci::resource
