// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/script.cpp (SCI0 late paths)
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

#include "script.hpp"

#include "resources.hpp"
#include "script_patcher.hpp"
#include "seg_manager.hpp"

#include <format>

namespace sci::vm {

void Script::free_script(bool keep_locals_segment) {
    number_ = 0;
    buffer_.clear();
    exports_offset_ = 0;
    num_exports_ = 0;
    synonyms_offset_ = 0;
    num_synonyms_ = 0;
    locals_offset_ = 0;
    if (!keep_locals_segment) {
        locals_segment_ = 0;
    }
    locals_block_ = nullptr;
    locals_count_ = 0;
    lockers_ = 1;
    marked_deleted_ = false;
    objects_.clear();
}

std::uint16_t Script::read16(std::uint32_t offset) const {
    if (offset + 1 >= buffer_.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(buffer_[offset] | (buffer_[offset + 1] << 8U));
}

bool Script::load(int number, Resources& resources, const ScriptPatcher* patcher, std::string& message) {
    free_script();
    const std::vector<std::uint8_t>* data = resources.find(ResourceType::Script, number);
    if (data == nullptr) {
        message = std::format("Script {} not found", number);
        return false;
    }
    number_ = number;
    buffer_ = *data;
    if (patcher != nullptr) {
        patcher->process(number, buffer_);
    }

    // Some scripts have two export tables, of which the first is broken;
    // the last one counts.
    if (const auto exports = find_block(ScriptBlock::Exports, true)) {
        exports_offset_ = exports->offset + 6;
        num_exports_ = read16(exports->offset + 4);
    }
    if (const auto synonyms = find_block(ScriptBlock::Synonyms)) {
        synonyms_offset_ = synonyms->offset + 4;
        num_synonyms_ = static_cast<std::uint16_t>((synonyms->size - 4) / 4);
    }
    if (const auto locals = find_block(ScriptBlock::LocalVars)) {
        locals_offset_ = static_cast<int>(locals->offset + 4);
        locals_count_ = static_cast<std::uint16_t>((locals->size / 2) - 2);
    }
    if (locals_count_ == 0) {
        locals_offset_ = 0;
    }
    if (locals_offset_ + (locals_count_ * 2) + 1 >= static_cast<int>(buffer_.size())) {
        message = std::format("Locals extend beyond end of script {}", number);
        return false;
    }
    return true;
}

std::optional<Script::Block> Script::find_block(ScriptBlock type, bool last) const {
    std::optional<Block> found;
    std::uint32_t offset = 0;
    while (offset + 4 <= buffer_.size()) {
        const std::uint16_t block_type = read16(offset);
        if (block_type == 0) {
            break;
        }
        const std::uint16_t block_size = read16(offset + 2);
        if (block_size == 0) {
            break;
        }
        if (block_type == static_cast<std::uint16_t>(type)) {
            found = Block{.offset = offset, .size = block_size};
            if (!last) {
                break;
            }
        }
        offset += block_size;
    }
    return found;
}

std::vector<std::uint16_t> Script::relocation_table() const {
    const auto block = find_block(ScriptBlock::Pointers);
    if (!block) {
        return {};
    }
    const std::uint16_t count = read16(block->offset + 4);
    if (count == 0) {
        return {};
    }
    std::uint32_t data = block->offset + 6;
    // Later SCI1 tables start with a null entry outside the count.
    if (read16(data) == 0) {
        data += 2;
    }
    if (data + (count * 2U) != block->offset + block->size) {
        warn(std::format("Script {} unexpected relocation table size {}", number_, block->size));
    }
    std::vector<std::uint16_t> entries;
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        entries.push_back(read16(data + (i * 2)));
    }
    return entries;
}

std::optional<std::uint32_t> Script::relocation_offset(std::uint32_t offset) const {
    for (const std::uint16_t entry : relocation_table()) {
        if (entry == offset) {
            return 0;
        }
    }
    return std::nullopt;
}

bool Script::relocate_local(SegmentId segment, int location) {
    if (locals_block_ == nullptr) {
        return false;
    }
    const int relative = location - locals_offset_;
    if (relative < 0) {
        return false;
    }
    const auto index = static_cast<std::size_t>(relative >> 1);
    if (index >= locals_block_->locals.size() || (relative & 1) != 0) {
        return false;
    }
    locals_block_->locals[index].segment = segment;
    return true;
}

void Script::relocate(SegmentId segment) {
    for (const std::uint16_t position : relocation_table()) {
        // Locals and objects are relocated; code blocks are not.
        if (!relocate_local(segment, position)) {
            for (auto& [offset, object] : objects_) {
                if (object.relocate(segment, position, 0)) {
                    break;
                }
            }
        }
    }
}

void Script::increment_lockers() {
    if (lockers_ == 0xFFFFFFFF) {
        warn(std::format("script {} locker maximum reached, resetting", number_));
        lockers_ = 0x80000000;
    }
    ++lockers_;
}

void Script::decrement_lockers() {
    if (lockers_ > 0) {
        --lockers_;
    }
}

std::optional<std::uint32_t> Script::export_offset(int index) const {
    if (index < 0 || index >= num_exports_) {
        return std::nullopt;
    }
    const std::uint16_t offset = read16(exports_offset_ + (static_cast<std::uint32_t>(index) * 2));
    if (offset >= buffer_.size()) {
        return std::nullopt;
    }
    return offset;
}

std::span<const std::uint8_t> Script::synonyms() const {
    if (num_synonyms_ == 0) {
        return {};
    }
    return std::span<const std::uint8_t>(buffer_).subspan(synonyms_offset_, num_synonyms_ * 4U);
}

SegmentRef Script::dereference(Reg pointer) {
    SegmentRef ref;
    if (pointer.offset > buffer_.size()) {
        return ref;
    }
    ref.is_raw = true;
    ref.max_size = static_cast<int>(buffer_.size() - pointer.offset);
    ref.raw = std::span<std::uint8_t>(buffer_).subspan(pointer.offset);
    return ref;
}

bool Script::offset_is_object(std::uint32_t offset) const {
    return offset >= static_cast<std::uint32_t>(-object_magic_offset) &&
           read16(offset - static_cast<std::uint32_t>(-object_magic_offset)) == object_magic_number;
}

Object* Script::object_at(std::uint32_t offset) {
    const auto it = objects_.find(offset);
    return it == objects_.end() ? nullptr : &it->second;
}

const Object* Script::object_at(std::uint32_t offset) const {
    const auto it = objects_.find(offset);
    return it == objects_.end() ? nullptr : &it->second;
}

Object* Script::init_object(Reg position) {
    Object& object = objects_[position.offset];
    object.init(*this, position);
    return &object;
}

void Script::initialize_locals(SegManager& segments) {
    if (locals_count_ == 0) {
        return;
    }
    LocalVariables* locals = nullptr;
    if (locals_segment_ == 0) {
        auto owned = std::make_unique<LocalVariables>();
        locals = owned.get();
        locals_segment_ = segments.alloc_segment(std::move(owned));
    } else {
        locals = dynamic_cast<LocalVariables*>(segments.segment(locals_segment_, SegmentType::Locals));
        if (locals == nullptr || locals->script_id != number_) {
            segments.faults().raise(std::format("Invalid script {} locals segment while allocating locals", number_));
            return;
        }
    }
    locals_block_ = locals;
    locals->script_id = number_;
    locals->locals.resize(locals_count_);
    for (std::uint16_t i = 0; i < locals_count_; ++i) {
        locals->locals[i] = vm::number(read16(static_cast<std::uint32_t>(locals_offset_) + (i * 2U)));
    }
}

void Script::initialize_class(SegManager& segments, std::uint16_t species, std::uint32_t position) {
    // The class table (vocab 996) is incomplete in some games; plausible
    // species beyond it grow the table, as the original interpreter wrote
    // past its end.
    if (species >= segments.class_table_size()) {
        if (species <= 255) {
            warn(std::format("Resizing class table for species {} in script {}", species, number_));
            segments.resize_class_table(species + 1U);
        } else {
            segments.faults().raise(std::format("Invalid species {} in script {}", species, number_));
            return;
        }
    }
    segments.set_class_offset(species, make_reg(segments.script_segment(number_), static_cast<int>(position)));
}

void Script::initialize_objects(SegManager& segments, SegmentId segment) {
    std::uint32_t seeker = 0;
    do {
        const std::uint16_t type = read16(seeker);
        if (type == 0) {
            break;
        }
        if (type == static_cast<std::uint16_t>(ScriptBlock::Object) ||
            type == static_cast<std::uint16_t>(ScriptBlock::Class)) {
            const auto position = static_cast<std::uint16_t>(seeker + 12);
            if (type == static_cast<std::uint16_t>(ScriptBlock::Class)) {
                initialize_class(segments, read16(seeker + 12), position);
            }
            const Reg address = make_reg(segment, position);
            Object* object = init_object(address);
            object->init_species(segments, address);
            if (!object->init_base_object(segments, address)) {
                segments.faults().raise(std::format("Failed to locate base object for object at {:04x}:{:04x} in "
                                                    "script {}",
                                                    address.segment, address.offset, number_));
                return;
            }
        }
        const std::uint16_t block_size = read16(seeker + 2);
        if (block_size == 0) {
            break;
        }
        seeker += block_size;
    } while (seeker < size() - 2);
    relocate(segment);
}

void Script::free_at_address(SegManager& segments, Reg /*address*/) {
    if (marked_deleted_) {
        segments.deallocate_script(number_);
    }
}

std::vector<Reg> Script::list_all_outgoing_references(Reg address) const {
    std::vector<Reg> result;
    if (address.offset <= buffer_.size() && offset_is_object(address.offset)) {
        if (const Object* object = object_at(address.offset)) {
            if (locals_segment_ != 0) {
                result.push_back(make_reg(locals_segment_, 0));
            }
            for (std::size_t i = 0; i < object->var_count(); ++i) {
                result.push_back(object->variable(i));
            }
        }
    }
    return result;
}

std::vector<Reg> Script::list_object_references() const {
    std::vector<Reg> result;
    if (locals_segment_ != 0) {
        result.push_back(make_reg(locals_segment_, 0));
    }
    for (const auto& [offset, object] : objects_) {
        result.push_back(object.pos());
    }
    return result;
}

} // namespace sci::vm
