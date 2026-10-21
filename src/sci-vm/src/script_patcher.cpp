// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/script_patches.cpp (ScriptPatcher)
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

#include "script_patcher.hpp"

#include "faults.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace sci::vm {

namespace pc = patch_code;

namespace {

// PATCH_VALUELIMIT: the original bytes a patch can read.
constexpr std::size_t patch_value_limit = 4096;

std::uint16_t word_at(std::span<const std::uint16_t> data, std::size_t index) {
    return index < data.size() ? data[index] : pc::end;
}

} // namespace

void ScriptPatcher::fault(std::string message) const {
    if (fault_) {
        fault_(std::move(message));
    } else {
        warn(message);
    }
}

int ScriptPatcher::selector(std::uint16_t id) const {
    return id < selector_ids_.size() ? selector_ids_[id] : -1;
}

int ScriptPatcher::resolve_selector(std::uint16_t id) const {
    if (id >= selector_ids_.size()) {
        fault(std::format("Script-Patcher: unknown selector id {}", id));
        return -1;
    }
    if (selector_ids_[id] == -1 && selectors_) {
        selector_ids_[id] = selectors_(table_->selector_names[id]);
    }
    return selector_ids_[id];
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to ScriptPatcher::applyPatch.
void ScriptPatcher::apply_patch(std::span<const std::uint16_t> patch, std::span<std::uint8_t> data,
                                std::size_t offset) const {
    // The original bytes, for the commands that copy them.
    std::array<std::uint8_t, patch_value_limit> original{};
    const std::size_t original_size = std::min(data.size() - std::min(offset, data.size()), patch_value_limit);
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), original_size, original.begin());
    const auto put = [&](std::size_t at, std::uint8_t value) {
        if (at >= data.size()) {
            fault("Script-Patcher: patch beyond the end of the script");
            return false;
        }
        data[at] = value;
        return true;
    };
    std::size_t index = 0;
    std::uint16_t word = word_at(patch, index);
    while (word != pc::end) {
        const auto command = static_cast<std::uint16_t>(word & pc::command_mask);
        const auto value = static_cast<std::uint16_t>(word & pc::value_mask);
        switch (command) {
        case pc::add_to_offset:
            offset += value;
            break;
        case pc::get_original_bytes: {
            const std::uint16_t length = word_at(patch, ++index);
            if (std::size_t{value} + length >= original_size) {
                fault("Script-Patcher: can not get requested original byte from script");
                return;
            }
            for (std::size_t i = 0; i < length; ++i) {
                if (!put(offset + i, original[value + i])) {
                    return;
                }
            }
            offset += length;
            break;
        }
        case pc::get_original_byte: {
            if (value >= original_size) {
                fault("Script-Patcher: can not get requested original byte from script");
                return;
            }
            const auto adjust = static_cast<std::int16_t>(word_at(patch, ++index));
            if (!put(offset++, static_cast<std::uint8_t>(original[value] + adjust))) {
                return;
            }
            break;
        }
        case pc::get_original_uint16: {
            if (value >= original_size || std::size_t{value} + 1 >= original_size) {
                fault("Script-Patcher: can not get requested original uint16 from script");
                return;
            }
            const auto adjust = static_cast<std::int16_t>(word_at(patch, ++index));
            const auto result = static_cast<std::uint16_t>(
                (unsigned{original[value]} | (unsigned{original[value + 1]} << 8U)) + static_cast<unsigned>(adjust));
            if (!put(offset, static_cast<std::uint8_t>(result & 0xFFU)) ||
                !put(offset + 1, static_cast<std::uint8_t>(result >> 8U))) {
                return;
            }
            offset += 2;
            break;
        }
        case pc::uint16:
        case pc::selector16: {
            std::uint8_t low = 0;
            std::uint8_t high = 0;
            if (command == pc::uint16) {
                low = static_cast<std::uint8_t>(value & pc::byte_mask);
                word = word_at(patch, ++index);
                if ((word & pc::command_mask) != 0) {
                    fault("Script-Patcher: Patch inconsistent");
                    return;
                }
                high = static_cast<std::uint8_t>(word & pc::byte_mask);
            } else {
                const auto selector_value = static_cast<std::uint16_t>(selector(value));
                low = static_cast<std::uint8_t>(selector_value & 0xFFU);
                high = static_cast<std::uint8_t>(selector_value >> 8U);
            }
            if (!put(offset, low) || !put(offset + 1, high)) {
                return;
            }
            offset += 2;
            break;
        }
        case pc::selector8: {
            const auto selector_value = static_cast<std::uint16_t>(selector(value));
            if ((selector_value & 0xFF00U) != 0) {
                fault("Script-Patcher: 8 bit selector required, game uses 16 bit selector");
                return;
            }
            if (!put(offset++, static_cast<std::uint8_t>(selector_value))) {
                return;
            }
            break;
        }
        case pc::byte:
            if (!put(offset++, static_cast<std::uint8_t>(value & pc::byte_mask))) {
                return;
            }
            break;
        default:
            break;
        }
        word = word_at(patch, ++index);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to ScriptPatcher::verifySignature.
bool ScriptPatcher::verify_signature(std::size_t offset, std::span<const std::uint16_t> signature,
                                     std::span<const std::uint8_t> data) const {
    std::size_t index = 0;
    std::uint16_t word = word_at(signature, index);
    while (word != pc::end) {
        const auto command = static_cast<std::uint16_t>(word & pc::command_mask);
        const auto value = static_cast<std::uint16_t>(word & pc::value_mask);
        switch (command) {
        case pc::add_to_offset:
            offset += value;
            break;
        case pc::uint16:
        case pc::selector16:
            if (offset + 1 < data.size()) {
                std::uint8_t low = 0;
                std::uint8_t high = 0;
                if (command == pc::uint16) {
                    low = static_cast<std::uint8_t>(value & pc::byte_mask);
                    word = word_at(signature, ++index);
                    if ((word & pc::command_mask) != 0) {
                        fault("Script-Patcher: signature inconsistent");
                        return false;
                    }
                    high = static_cast<std::uint8_t>(word & pc::byte_mask);
                } else {
                    const auto selector_value = static_cast<std::uint16_t>(selector(value));
                    low = static_cast<std::uint8_t>(selector_value & 0xFFU);
                    high = static_cast<std::uint8_t>(selector_value >> 8U);
                }
                if (data[offset] != low || data[offset + 1] != high) {
                    word = pc::mismatch;
                }
                offset += 2;
            } else {
                word = pc::mismatch;
            }
            break;
        case pc::selector8:
            if (offset < data.size()) {
                const auto selector_value = static_cast<std::uint16_t>(selector(value));
                if ((selector_value & 0xFF00U) != 0) {
                    fault("Script-Patcher: 8 bit selector required, game uses 16 bit selector");
                    return false;
                }
                if (data[offset] != (selector_value & 0xFFU)) {
                    word = pc::mismatch;
                }
                ++offset;
            } else {
                word = pc::mismatch;
            }
            break;
        case pc::byte:
            if (offset < data.size()) {
                if (data[offset] != word) {
                    word = pc::mismatch;
                }
                ++offset;
            } else {
                word = pc::mismatch;
            }
            break;
        default:
            break;
        }
        if (word == pc::mismatch) {
            break;
        }
        word = word_at(signature, ++index);
    }
    return word == pc::end;
}

std::optional<std::size_t> ScriptPatcher::find_signature(std::uint32_t magic, int magic_offset,
                                                         std::span<const std::uint16_t> signature,
                                                         std::span<const std::uint8_t> data) const {
    if (data.size() < 4) {
        return std::nullopt;
    }
    const std::size_t limit = data.size() - 3;
    for (std::size_t at = 0; at < limit; ++at) {
        const std::uint32_t dword = std::uint32_t{data[at]} | (std::uint32_t{data[at + 1]} << 8U) |
                                    (std::uint32_t{data[at + 2]} << 16U) |
                                    (static_cast<std::uint32_t>(data[at + 3]) << 24U);
        if (dword != magic) {
            continue;
        }
        // An offset before the script start wraps around in ScummVM and
        // cannot match.
        const auto start = static_cast<std::ptrdiff_t>(at) + magic_offset;
        if (start >= 0 && verify_signature(static_cast<std::size_t>(start), signature, data)) {
            return static_cast<std::size_t>(start);
        }
    }
    return std::nullopt;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to calculateMagicDWordAndVerify.
void ScriptPatcher::calculate_magic(std::span<const std::uint16_t> data, bool magic_included, std::uint32_t& magic,
                                    int& magic_offset) const {
    int position = 0;
    std::array<std::uint8_t, 4> bytes{};
    int left = 0;
    const auto remember = [&](std::uint8_t value) {
        bytes[static_cast<std::size_t>(4 - left)] = value;
        --left;
        if (left == 0) {
            magic = std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8U) | (std::uint32_t{bytes[2]} << 16U) |
                    (std::uint32_t{bytes[3]} << 24U);
        }
    };
    std::size_t index = 0;
    std::uint16_t word = word_at(data, index);
    while (word != pc::end) {
        const auto command = static_cast<std::uint16_t>(word & pc::command_mask);
        const auto value = static_cast<std::uint16_t>(word & pc::value_mask);
        switch (command) {
        case pc::magic_dword:
            if (!magic_included) {
                fault("Script-Patcher: Magic-DWORD sequence found in patch data");
                return;
            }
            if (magic != 0 || left != 0) {
                fault("Script-Patcher: Magic-DWORD specified multiple times in signature");
                return;
            }
            left = 4;
            magic_offset = position;
            break;
        case pc::add_to_offset:
            position -= value;
            if (left != 0) {
                fault("Script-Patcher: Magic-DWORD contains AddToOffset command");
                return;
            }
            break;
        case pc::uint16:
        case pc::selector16: {
            std::uint8_t low = 0;
            std::uint8_t high = 0;
            if (command == pc::uint16) {
                word = word_at(data, ++index);
                if ((word & pc::command_mask) != 0) {
                    fault("Script-Patcher: signature entry inconsistent");
                    return;
                }
                low = static_cast<std::uint8_t>(value);
                high = static_cast<std::uint8_t>(word & pc::byte_mask);
            } else {
                const auto selector_value = static_cast<std::uint16_t>(resolve_selector(value));
                low = static_cast<std::uint8_t>(selector_value & 0xFFU);
                high = static_cast<std::uint8_t>(selector_value >> 8U);
            }
            position -= 2;
            if (left != 0) {
                remember(low);
                if (left != 0) {
                    remember(high);
                }
            }
            break;
        }
        case pc::byte:
        case pc::selector8: {
            int byte_value = value;
            if (command == pc::selector8) {
                byte_value = resolve_selector(value);
                if (byte_value != -1 && (static_cast<unsigned>(byte_value) & 0xFF00U) != 0) {
                    fault("Script-Patcher: 8 bit selector required, game uses 16 bit selector");
                    return;
                }
            }
            --position;
            if (left != 0) {
                remember(static_cast<std::uint8_t>(byte_value));
            }
            break;
        }
        case pc::get_original_byte:
        case pc::get_original_uint16:
            ++index; // The adjustment.
            break;
        default:
            break;
        }
        word = word_at(data, ++index);
    }
    if (left != 0) {
        fault("Script-Patcher: Magic-DWORD beyond End-Of-Signature");
        return;
    }
    if (magic_included && magic == 0) {
        fault("Script-Patcher: Magic-DWORD not specified in signature");
    }
}

void ScriptPatcher::init() const {
    initialized_ = true;
    runtime_.clear();
    for (const ScriptPatchEntry& entry : table_->entries) {
        Runtime runtime{.active = entry.default_active, .magic = 0, .magic_offset = 0};
        calculate_magic(entry.signature, true, runtime.magic, runtime.magic_offset);
        calculate_magic(entry.patch, false, runtime.magic, runtime.magic_offset);
        runtime_.push_back(runtime);
    }
}

void ScriptPatcher::process(int number, std::vector<std::uint8_t>& buffer) const {
    if (!initialized_) {
        init();
    }
    for (std::size_t i = 0; i < table_->entries.size(); ++i) {
        const ScriptPatchEntry& entry = table_->entries[i];
        const Runtime& runtime = runtime_[i];
        if (std::cmp_not_equal(entry.script, number) || !runtime.active) {
            continue;
        }
        std::int16_t count = entry.apply_count;
        std::optional<std::size_t> found;
        do {
            found = find_signature(runtime.magic, runtime.magic_offset, entry.signature, buffer);
            if (found) {
                applied_.push_back(std::format("{} on script {} offset {}", entry.description, number, *found));
                debug(std::format("Script-Patcher: '{}' on script {} offset {}", entry.description, number, *found));
                apply_patch(entry.patch, buffer, *found);
            }
            --count;
        } while (found && count != 0);
    }
}

} // namespace sci::vm
