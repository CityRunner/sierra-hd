// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/seg_manager.h, engines/sci/engine/seg_manager.cpp
//   (SCI0 paths)
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

#include "faults.hpp"
#include "object.hpp"
#include "resources.hpp"
#include "script.hpp"
#include "segments.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sci::vm {

class StateWriter;
class StateReader;
class ScriptPatcher;

// Whether getting a script's segment loads the script, and whether it
// locks it (adds a locker).
enum class ScriptLoad : std::uint8_t {
    DontLoad = 0,
    Load = 1,
    Lock = 3,
};

struct Class {
    int script = 0; // The script that defines the class.
    Reg reg;        // Its address, null while the script is not loaded.
};

class SegManager {
public:
    SegManager(Resources& resources, const ScriptPatcher* patcher, Faults& faults);

    // Frees everything and reloads the class table (kRestartGame).
    void reset();

    [[nodiscard]] Faults& faults() { return *faults_; }

    // Segments.
    SegmentId alloc_segment(std::unique_ptr<Segment> segment);
    void deallocate(SegmentId id);
    [[nodiscard]] Segment* segment(SegmentId id) const;
    [[nodiscard]] Segment* segment(SegmentId id, SegmentType type) const;
    [[nodiscard]] SegmentType segment_type(SegmentId id) const;
    [[nodiscard]] SegmentId find_segment_by_type(SegmentType type) const;

    [[nodiscard]] std::size_t heap_size() const { return heap_.size(); }

    // Scripts.
    [[nodiscard]] Script* script(SegmentId id) const; // A fault if not a script.
    [[nodiscard]] Script* script_if_loaded(SegmentId id) const;
    [[nodiscard]] SegmentId script_segment(int number) const;
    SegmentId script_segment(int number, ScriptLoad load);
    // Loads a script (or adds a locker if it is loaded); its segment, or 0.
    SegmentId instantiate_script(int number);
    void uninstantiate_script(int number);

    void deallocate_script(int number) { deallocate(script_segment(number)); }

    // Objects and classes.
    [[nodiscard]] Object* object(Reg pos) const;
    [[nodiscard]] bool is_heap_object(Reg pos) const;
    [[nodiscard]] std::string object_name(Reg pos) const;
    Reg class_address(int class_number, ScriptLoad load, SegmentId caller_segment);

    [[nodiscard]] std::size_t class_table_size() const { return classes_.size(); }

    [[nodiscard]] const Class& class_at(std::size_t index) const { return classes_[index]; }

    void set_class_offset(std::size_t index, Reg reg) { classes_[index].reg = reg; }

    void resize_class_table(std::size_t size) { classes_.resize(size); }

    // Dynamic tables.
    DataStack* allocate_stack(int size);
    Object* allocate_clone(Reg& address);
    List* allocate_list(Reg& address);
    Node* allocate_node(Reg& address);
    Reg new_node(Reg value, Reg key);
    [[nodiscard]] List* lookup_list(Reg address) const;
    [[nodiscard]] Node* lookup_node(Reg address, bool stop_on_discarded = true) const;
    Reg allocate_hunk_entry(std::string_view type, std::size_t size);
    void free_hunk_entry(Reg address) const;
    [[nodiscard]] Hunk* hunk(Reg address) const;
    std::span<std::uint8_t> allocate_dynmem(std::size_t size, std::string_view description, Reg& address);
    bool free_dynmem(Reg address);

    // System strings: the save directory and the parser's buffer.
    void init_sys_strings();
    // What ScummVM's gamestate_restore leaves different from the saved
    // segments: it neither saves nor loads the hunk table, so the hunk
    // segment is gone (the saved handles point nowhere) and the next hunk
    // gets a new segment, and it allocates the system strings again in a
    // new segment (the saved one stays).
    void after_restore();

    [[nodiscard]] Reg save_dir_pointer() const { return save_dir_pointer_; }

    [[nodiscard]] Reg parser_pointer() const { return parser_pointer_; }

    // Memory access through pointers.
    [[nodiscard]] SegmentRef dereference(Reg pointer) const;
    // Raw bytes or registers at a pointer, at least `entries` of them;
    // empty with a warning otherwise.
    [[nodiscard]] std::span<std::uint8_t> deref_bulk(Reg pointer, int entries) const;
    [[nodiscard]] std::span<Reg> deref_regs(Reg pointer, int entries) const;
    void strncpy(Reg dest, std::string_view source, std::size_t n) const;
    void strncpy(Reg dest, Reg source, std::size_t n) const;

    void strcpy(Reg dest, std::string_view source) const { strncpy(dest, source, unbounded); }

    void strcpy(Reg dest, Reg source) const { strncpy(dest, source, unbounded); }

    void memcpy(Reg dest, std::span<const std::uint8_t> source) const;
    void memcpy(Reg dest, Reg source, std::size_t n) const;
    void memcpy(std::span<std::uint8_t> dest, Reg source) const;
    [[nodiscard]] std::size_t strlen(Reg str) const;
    [[nodiscard]] std::string get_string(Reg pointer) const;

    static constexpr std::size_t unbounded = 0xFFFFFFFF;

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;
    // Reads it back (state_load.cpp).
    void load_state(StateReader& in);

private:
    [[nodiscard]] SegmentId find_free_segment() const;
    Script* allocate_script(int number, SegmentId& segment);
    void create_class_table();
    void uninstantiate_script_sci0(int number);
    template <typename T>
    T* table(SegmentId& id);

    Resources* resources_;
    const ScriptPatcher* patcher_;
    Faults* faults_;
    std::vector<std::unique_ptr<Segment>> heap_;
    std::map<int, SegmentId> script_segments_;
    std::vector<Class> classes_;
    SegmentId clones_ = 0;
    SegmentId lists_ = 0;
    SegmentId nodes_ = 0;
    SegmentId hunks_ = 0;
    Reg save_dir_pointer_{};
    Reg parser_pointer_{};
};

} // namespace sci::vm
