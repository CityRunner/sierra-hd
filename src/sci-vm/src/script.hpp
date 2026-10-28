// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/script.h, engines/sci/engine/script.cpp (SCI0 late
//   paths)
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

#include "object.hpp"
#include "segments.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sci::vm {

class StateWriter;
class Resources;
class ScriptPatcher;

// The block types of an SCI0 script.
enum class ScriptBlock : std::uint8_t {
    Terminator = 0,
    Object = 1,
    Code = 2,
    Synonyms = 3,
    Said = 4,
    Strings = 5,
    Class = 6,
    Exports = 7,
    Pointers = 8,
    PreloadText = 9,
    LocalVars = 10,
};

inline constexpr std::uint16_t object_magic_number = 0x1234;
// Objects are addressed 8 bytes after their magic number in SCI0.
inline constexpr int object_magic_offset = -8;

class Script final : public Segment {
public:
    Script() : Segment(SegmentType::Script) {}

    // Resets to the unloaded state, keeping the locals segment if asked.
    void free_script(bool keep_locals_segment = false);
    // Loads script `number` and applies the script patches. False (with a
    // message) if the script is missing or malformed.
    bool load(int number, Resources& resources, const ScriptPatcher* patcher, std::string& message);

    [[nodiscard]] bool is_valid_offset(std::uint32_t offset) const override { return offset < buffer_.size(); }

    [[nodiscard]] SegmentRef dereference(Reg pointer) override;

    [[nodiscard]] Reg find_canonic_address(const SegManager& /*segments*/, Reg address) const override {
        return make_reg(address.segment, 0);
    }

    void free_at_address(SegManager& segments, Reg address) override;

    [[nodiscard]] std::vector<Reg> list_all_deallocatable(SegmentId id) const override { return {make_reg(id, 0)}; }

    [[nodiscard]] std::vector<Reg> list_all_outgoing_references(Reg address) const override;
    // The references of the script's locals and objects (GC roots).
    [[nodiscard]] std::vector<Reg> list_object_references() const;

    [[nodiscard]] int number() const { return number_; }

    [[nodiscard]] std::span<const std::uint8_t> buffer() const { return buffer_; }

    [[nodiscard]] std::span<std::uint8_t> buffer() { return buffer_; }

    [[nodiscard]] std::uint32_t size() const { return static_cast<std::uint32_t>(buffer_.size()); }

    [[nodiscard]] std::uint16_t read16(std::uint32_t offset) const;

    [[nodiscard]] int locals_offset() const { return locals_offset_; }

    [[nodiscard]] std::uint16_t locals_count() const { return locals_count_; }

    [[nodiscard]] SegmentId locals_segment() const { return locals_segment_; }

    [[nodiscard]] LocalVariables* locals_block() const { return locals_block_; }

    [[nodiscard]] std::map<std::uint32_t, Object>& objects() { return objects_; }

    [[nodiscard]] const std::map<std::uint32_t, Object>& objects() const { return objects_; }

    [[nodiscard]] bool offset_is_object(std::uint32_t offset) const;
    [[nodiscard]] Object* object_at(std::uint32_t offset);
    [[nodiscard]] const Object* object_at(std::uint32_t offset) const;

    void initialize_locals(SegManager& segments);
    void initialize_objects(SegManager& segments, SegmentId segment);

    void increment_lockers();
    void decrement_lockers();

    [[nodiscard]] std::uint32_t lockers() const { return lockers_; }

    void set_lockers(std::uint32_t lockers) { lockers_ = lockers; }

    [[nodiscard]] std::uint16_t exports_count() const { return num_exports_; }

    // The code offset of an export, or nullopt if the export is invalid.
    [[nodiscard]] std::optional<std::uint32_t> export_offset(int index) const;
    [[nodiscard]] std::span<const std::uint8_t> synonyms() const;

    [[nodiscard]] std::uint16_t synonyms_count() const { return num_synonyms_; }

    void mark_deleted() { marked_deleted_ = true; }

    [[nodiscard]] bool is_marked_deleted() const { return marked_deleted_; }

    // The offset added to `offset` by relocation (0 in SCI0), if the
    // relocation table lists it.
    [[nodiscard]] std::optional<std::uint32_t> relocation_offset(std::uint32_t offset) const;

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;

private:
    struct Block {
        std::uint32_t offset = 0; // Of the block header.
        std::uint32_t size = 0;   // Including the header.
    };

    [[nodiscard]] std::optional<Block> find_block(ScriptBlock type, bool last = false) const;
    [[nodiscard]] std::vector<std::uint16_t> relocation_table() const;
    void relocate(SegmentId segment);
    bool relocate_local(SegmentId segment, int location);
    Object* init_object(Reg position);
    void initialize_class(SegManager& segments, std::uint16_t species, std::uint32_t position);

    int number_ = 0;
    std::vector<std::uint8_t> buffer_;
    std::uint32_t exports_offset_ = 0; // Of the first export entry.
    std::uint16_t num_exports_ = 0;
    std::uint32_t synonyms_offset_ = 0;
    std::uint16_t num_synonyms_ = 0;
    int locals_offset_ = 0;
    std::uint16_t locals_count_ = 0;
    std::uint32_t lockers_ = 1;
    bool marked_deleted_ = false;
    SegmentId locals_segment_ = 0;
    LocalVariables* locals_block_ = nullptr;
    std::map<std::uint32_t, Object> objects_;
};

} // namespace sci::vm
