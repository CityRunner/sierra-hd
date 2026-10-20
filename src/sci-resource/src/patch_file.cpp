// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/resource/resource.cpp (ResourceManager::readResourcePatches,
//       ResourceManager::processPatch)
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

#include "sci/resource/patch_file.hpp"

#include "ascii.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace sci::resource {

namespace {

using MatchFunction = bool (*)(std::string_view name, ResourceType type);

// SCI0-style mask "<type>.###": the type name, a dot and three digits.
bool matches_sci0_mask(std::string_view name, ResourceType type) {
    const std::string_view type_name = resource_type_name(type);
    return name.size() == type_name.size() + 4 && ascii::istarts_with(name, type_name) &&
           name[type_name.size()] == '.' && std::ranges::all_of(name.substr(type_name.size() + 1), ascii::is_digit);
}

// SCI1-style mask "*.<suffix>", where the wildcard may be empty.
bool matches_suffix(std::string_view name, std::string_view suffix) {
    return name.size() > suffix.size() && name[name.size() - suffix.size() - 1] == '.' &&
           ascii::iends_with(name, suffix);
}

// The extra SCI1 masks ScummVM lists for some types: EGA and Amiga views and
// pictures, and SCI3 scripts.
std::span<const std::string_view> extra_suffixes(ResourceType type) {
    static constexpr std::array<std::string_view, 3> view_suffixes{"v16", "v32", "v64"};
    static constexpr std::array<std::string_view, 3> pic_suffixes{"p16", "p32", "p64"};
    static constexpr std::array<std::string_view, 1> script_suffixes{"csc"};
    switch (type) {
    case ResourceType::View:
        return view_suffixes;
    case ResourceType::Pic:
        return pic_suffixes;
    case ResourceType::Script:
        return script_suffixes;
    default:
        return {};
    }
}

// strtol(text, &end, 10) for text starting with a digit: the value
// saturates, and the result says whether a '.' follows the digits.
std::pair<std::int64_t, bool> parse_leading_number(std::string_view text) {
    std::int64_t value = 0;
    std::size_t position = 0;
    for (; position < text.size() && ascii::is_digit(text[position]); ++position) {
        const std::int64_t digit = text[position] - '0';
        constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
        value = value > (max - digit) / 10 ? max : (value * 10) + digit;
    }
    return {value, ascii::char_at(text, position) == '.'};
}

// atoi(text): optional white space and sign, then digits. atoi's overflow is
// undefined; here the value saturates.
std::int64_t parse_atoi(std::string_view text) {
    std::size_t position = 0;
    while (position < text.size() && (text[position] == ' ' || (text[position] >= '\t' && text[position] <= '\r'))) {
        ++position;
    }
    bool negative = false;
    if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
        negative = text[position] == '-';
        ++position;
    }
    if (position >= text.size() || !ascii::is_digit(text[position])) {
        return 0;
    }
    const std::int64_t value = parse_leading_number(text.substr(position)).first;
    return negative ? -value : value;
}

// The number a matched file name gives, or nullopt if ScummVM skips it
// (readResourcePatches, the bAdd test). Numbers are truncated to 16 bits,
// as ScummVM stores them in a uint16.
std::optional<std::uint16_t> patch_number(std::string_view name, ResourceType type) {
    if (ascii::is_digit(ascii::char_at(name, 0))) {
        const auto [value, dot_follows] = parse_leading_number(name);
        if (!dot_follows) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(value);
    }
    const std::string_view type_name = resource_type_name(type);
    // ScummVM tests the character after the separator, not the separator.
    if (!ascii::istarts_with(name, type_name) || ascii::is_alpha(ascii::char_at(name, type_name.size() + 1))) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parse_atoi(name.substr(std::min(name.size(), type_name.size() + 1))));
}

// Appends the files matching one mask, in file name order.
void collect_matches(std::span<const std::string> file_names, ResourceType type, const auto& matches,
                     std::vector<PatchCandidate>& candidates) {
    std::vector<std::size_t> matched;
    for (std::size_t i = 0; i < file_names.size(); ++i) {
        if (matches(file_names[i])) {
            matched.push_back(i);
        }
    }
    std::ranges::stable_sort(matched, [&](std::size_t a, std::size_t b) { return file_names[a] < file_names[b]; });
    for (const std::size_t index : matched) {
        if (const auto number = patch_number(file_names[index], type)) {
            candidates.push_back(PatchCandidate{.file = index, .id = ResourceId{.type = type, .number = *number}});
        }
    }
}

// Patch files are typed as their non-base36 counterparts (processPatch).
ResourceType checked_type(ResourceType type) noexcept {
    switch (type) {
    case ResourceType::Audio36:
        return ResourceType::Audio;
    case ResourceType::Sync36:
        return ResourceType::Sync;
    default:
        return type;
    }
}

} // namespace

std::vector<PatchCandidate> find_patch_candidates(std::span<const std::string> file_names) {
    std::vector<PatchCandidate> candidates;
    // All types up to Translation; ScummVM skips Rave and the SCI2+ types.
    for (auto type = ResourceType::View; type <= ResourceType::Translation;
         type = static_cast<ResourceType>(static_cast<int>(type) + 1)) {
        collect_matches(
            file_names, type, [type](std::string_view name) { return matches_sci0_mask(name, type); }, candidates);
        collect_matches(
            file_names, type,
            [type](std::string_view name) { return matches_suffix(name, resource_type_suffix(type)); }, candidates);
        for (const std::string_view suffix : extra_suffixes(type)) {
            collect_matches(
                file_names, type, [suffix](std::string_view name) { return matches_suffix(name, suffix); }, candidates);
        }
    }
    return candidates;
}

std::optional<std::size_t> patch_data_offset(std::span<const std::uint8_t> file, ResourceType expected) {
    if (file.size() < 3) {
        return std::nullopt;
    }
    // A RIFF file is a WAVE audio patch. ScummVM then takes the header size
    // from the first byte, as it does not skip the type byte in that case.
    const bool riff = file.size() >= 4 && file[0] == 'R' && file[1] == 'I' && file[2] == 'F' && file[3] == 'F';
    const ResourceType type = riff ? ResourceType::Audio : resource_type_from_number(file[0]);
    const std::size_t offset = 2 + std::size_t{riff ? file[0] : file[1]};
    if (type != checked_type(expected) || offset >= file.size()) {
        return std::nullopt;
    }
    return offset;
}

} // namespace sci::resource
