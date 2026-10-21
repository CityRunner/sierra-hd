// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/script_patches.{h,cpp} (the patcher)
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

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sci::vm {

// Signature and patch words, as SIG_* and PATCH_* in ScummVM: a command in
// the top 4 bits and a value in the low 12.
namespace patch_code {
inline constexpr std::uint16_t end = 0xFFFF;
inline constexpr std::uint16_t mismatch = 0xFFFE;
inline constexpr std::uint16_t command_mask = 0xF000;
inline constexpr std::uint16_t value_mask = 0x0FFF;
inline constexpr std::uint16_t byte_mask = 0x00FF;
inline constexpr std::uint16_t magic_dword = 0xF000;
inline constexpr std::uint16_t add_to_offset = 0xE000;
inline constexpr std::uint16_t get_original_bytes = 0xB000;
inline constexpr std::uint16_t get_original_byte = 0xC000;
inline constexpr std::uint16_t get_original_uint16 = 0xD000;
inline constexpr std::uint16_t selector16 = 0x9000;
inline constexpr std::uint16_t selector8 = 0x8000;
inline constexpr std::uint16_t uint16 = 0x1000;
inline constexpr std::uint16_t byte = 0x0000;

// SIG_SELECTOR16/8: a selector, by index in the table's selector names.
[[nodiscard]] constexpr std::uint16_t sel16(std::uint16_t id) {
    return selector16 | id;
}

[[nodiscard]] constexpr std::uint16_t sel8(std::uint16_t id) {
    return selector8 | id;
}

// SIG_ADDTOOFFSET and PATCH_ADDTOOFFSET.
[[nodiscard]] constexpr std::uint16_t skip(std::uint16_t count) {
    return add_to_offset | count;
}

// SIG_UINT16 and PATCH_UINT16 are two words: the low byte, then the high one.
[[nodiscard]] constexpr std::uint16_t u16_low(std::uint16_t value) {
    return uint16 | (value & 0xFFU);
}

[[nodiscard]] constexpr std::uint16_t u16_high(std::uint16_t value) {
    return value >> 8U;
}

// PATCH_GETORIGINALBYTE(ADJUST) and PATCH_GETORIGINALUINT16(ADJUST), each
// followed by its adjustment as a 16-bit two's complement word.
[[nodiscard]] constexpr std::uint16_t original_byte(std::uint16_t at) {
    return get_original_byte | at;
}

[[nodiscard]] constexpr std::uint16_t original_uint16(std::uint16_t at) {
    return get_original_uint16 | at;
}

[[nodiscard]] constexpr std::uint16_t adjust(int value) {
    return static_cast<std::uint16_t>(value);
}
} // namespace patch_code

struct ScriptPatchEntry {
    bool default_active = true;
    std::uint16_t script = 0;
    const char* description = "";
    std::int16_t apply_count = 1; // How often to look for the signature; 0 for unlimited.
    std::span<const std::uint16_t> signature;
    std::span<const std::uint16_t> patch;
};

struct ScriptPatchTable {
    std::span<const ScriptPatchEntry> entries;
    std::span<const std::string_view> selector_names; // Indexed by the sel8/sel16 ids.
};

// The table ScummVM uses for Laura Bow 1 (laurabow1Signatures).
[[nodiscard]] const ScriptPatchTable& laurabow1_patches();

class ScriptPatcher {
public:
    using SelectorLookup = std::function<int(std::string_view)>;
    using FaultReport = std::function<void(std::string)>;

    explicit ScriptPatcher(const ScriptPatchTable& table = laurabow1_patches())
        : table_(&table), selector_ids_(table.selector_names.size(), -1) {}

    // Selector numbers come from the game's names; errors in the table or
    // in the data (ScummVM's error()) are reported to `fault`.
    void attach(SelectorLookup selectors, FaultReport fault) {
        selectors_ = std::move(selectors);
        fault_ = std::move(fault);
    }

    // processScript: applies, in table order, the active patches for script
    // `number` whose signatures are found.
    void process(int number, std::vector<std::uint8_t>& buffer) const;

    // verifySignature: whether `signature` matches `data` at `offset`.
    [[nodiscard]] bool verify_signature(std::size_t offset, std::span<const std::uint16_t> signature,
                                        std::span<const std::uint8_t> data) const;
    // findSignature: the offset of the first match, found by the magic dword.
    [[nodiscard]] std::optional<std::size_t> find_signature(std::uint32_t magic, int magic_offset,
                                                            std::span<const std::uint16_t> signature,
                                                            std::span<const std::uint8_t> data) const;
    // applyPatch at a signature's offset.
    void apply_patch(std::span<const std::uint16_t> patch, std::span<std::uint8_t> data, std::size_t offset) const;
    // calculateMagicDWordAndVerify: the four script bytes after
    // SIG_MAGICDWORD (read little-endian) and their offset from the start of
    // the signature, which is negative or 0.
    void calculate_magic(std::span<const std::uint16_t> data, bool magic_included, std::uint32_t& magic,
                         int& magic_offset) const;

    // The patches applied so far, as "<description> on script <n> offset <o>".
    [[nodiscard]] const std::vector<std::string>& applied() const { return applied_; }

private:
    struct Runtime {
        bool active = false;
        std::uint32_t magic = 0;
        int magic_offset = 0;
    };

    void init() const;
    [[nodiscard]] int selector(std::uint16_t id) const;
    [[nodiscard]] int resolve_selector(std::uint16_t id) const;
    void fault(std::string message) const;

    const ScriptPatchTable* table_;
    SelectorLookup selectors_;
    FaultReport fault_;
    // Built when the first script is loaded, as in ScummVM.
    mutable std::vector<Runtime> runtime_;
    mutable std::vector<int> selector_ids_;
    mutable bool initialized_ = false;
    mutable std::vector<std::string> applied_;
};

} // namespace sci::vm
