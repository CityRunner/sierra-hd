// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/segment.h, engines/sci/engine/segment.cpp (SCI0 paths)
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
#include "sci/vm/reg.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sci::vm {

class SegManager;

// The segment types of ScummVM, with their numbers.
enum class SegmentType : std::uint8_t {
    Invalid = 0,
    Script = 1,
    Clones = 2,
    Locals = 3,
    Stack = 4,
    Lists = 6,
    Nodes = 7,
    Hunk = 8,
    DynMem = 9,
};

// What a pointer refers to: raw bytes (scripts, dynamic memory), or
// registers (locals, the stack), from the referenced position to the end of
// the segment.
struct SegmentRef {
    bool is_raw = true;
    std::span<std::uint8_t> raw;
    std::span<Reg> regs;
    int max_size = 0;       // Bytes available from the reference.
    bool skip_byte = false; // The reference is the second byte of regs[0].

    [[nodiscard]] bool valid() const { return is_raw ? raw.data() != nullptr : regs.data() != nullptr; }
};

class Segment {
public:
    explicit Segment(SegmentType type) : type_(type) {}

    virtual ~Segment() = default;
    Segment(const Segment&) = delete;
    Segment& operator=(const Segment&) = delete;
    Segment(Segment&&) = delete;
    Segment& operator=(Segment&&) = delete;

    [[nodiscard]] SegmentType type() const { return type_; }

    [[nodiscard]] virtual bool is_valid_offset(std::uint32_t offset) const = 0;
    // An invalid reference if the segment cannot be dereferenced.
    [[nodiscard]] virtual SegmentRef dereference(Reg pointer);

    // Garbage collection (engines/sci/engine/gc.cpp): the address that
    // governs `address`, freeing it, the addresses that may be freed, and
    // the references an address holds.
    [[nodiscard]] virtual Reg find_canonic_address(const SegManager& segments, Reg address) const;
    virtual void free_at_address(SegManager& segments, Reg address);
    [[nodiscard]] virtual std::vector<Reg> list_all_deallocatable(SegmentId id) const;
    [[nodiscard]] virtual std::vector<Reg> list_all_outgoing_references(Reg address) const;

private:
    SegmentType type_;
};

class LocalVariables final : public Segment {
public:
    LocalVariables() : Segment(SegmentType::Locals) {}

    [[nodiscard]] bool is_valid_offset(std::uint32_t offset) const override { return offset < locals.size() * 2; }

    [[nodiscard]] SegmentRef dereference(Reg pointer) override;
    [[nodiscard]] Reg find_canonic_address(const SegManager& segments, Reg address) const override;
    [[nodiscard]] std::vector<Reg> list_all_outgoing_references(Reg address) const override;

    int script_id = 0;
    std::vector<Reg> locals;
};

class DataStack final : public Segment {
public:
    DataStack() : Segment(SegmentType::Stack) {}

    [[nodiscard]] bool is_valid_offset(std::uint32_t offset) const override { return offset < entries.size() * 2; }

    [[nodiscard]] SegmentRef dereference(Reg pointer) override;

    [[nodiscard]] Reg find_canonic_address(const SegManager& /*segments*/, Reg address) const override {
        return make_reg(address.segment, 0);
    }

    [[nodiscard]] std::vector<Reg> list_all_outgoing_references(Reg address) const override;

    std::vector<Reg> entries;
};

struct Node {
    Reg pred;
    Reg succ;
    Reg key;
    Reg value;
};

struct List {
    Reg first;
    Reg last;
    // The next node for each level of recursion of kListEachElementDo.
    std::array<Reg, 10> next_nodes{};
    int num_recursions = 0;
};

struct Hunk {
    std::vector<std::uint8_t> mem;
    bool has_memory = false; // False once freed by kMemory/kDisposeWindow, until the GC frees the entry.
    std::string type;
};

// A table of entries with a free list, as SegmentObjTable: freed entries
// are reused last-freed first, which the trace of segment offsets reflects.
template <typename T>
class Table : public Segment {
public:
    explicit Table(SegmentType type) : Segment(type) {}

    int alloc_entry() {
        ++entries_used_;
        if (first_free_ != invalid) {
            const int index = first_free_;
            Entry& entry = table_[static_cast<std::size_t>(index)];
            first_free_ = entry.next_free;
            entry.next_free = index;
            entry.data = std::make_unique<T>();
            return index;
        }
        const int index = static_cast<int>(table_.size());
        table_.push_back(Entry{.data = std::make_unique<T>(), .next_free = index});
        return index;
    }

    [[nodiscard]] bool is_valid_offset(std::uint32_t offset) const override {
        return is_valid_entry(static_cast<int>(offset));
    }

    [[nodiscard]] bool is_valid_entry(int index) const {
        return index >= 0 && static_cast<std::size_t>(index) < table_.size() &&
               table_[static_cast<std::size_t>(index)].next_free == index;
    }

    virtual void free_entry(int index) {
        Entry& entry = table_[static_cast<std::size_t>(index)];
        entry.next_free = first_free_;
        entry.data.reset();
        first_free_ = index;
        --entries_used_;
    }

    [[nodiscard]] std::vector<Reg> list_all_deallocatable(SegmentId id) const override {
        std::vector<Reg> result;
        for (std::size_t i = 0; i < table_.size(); ++i) {
            if (is_valid_entry(static_cast<int>(i))) {
                result.push_back(make_reg(id, static_cast<int>(i)));
            }
        }
        return result;
    }

    [[nodiscard]] std::size_t size() const { return table_.size(); }

    // The free list, for the VM state: the first free entry and each
    // entry's successor in it (its own index while in use), -1 ending it.
    [[nodiscard]] int first_free() const { return first_free_; }

    [[nodiscard]] int entries_used() const { return entries_used_; }

    [[nodiscard]] int next_free(std::size_t index) const { return table_[index].next_free; }

    // Restoring a saved table: `size` empty entries, then each entry's
    // successor (an entry in use gets an empty element). The caller checks
    // that the free list is consistent (valid_free_list).
    void restore(std::size_t size, int first_free, int entries_used) {
        table_.clear();
        table_.resize(size);
        first_free_ = first_free;
        entries_used_ = entries_used;
    }

    void restore_entry(std::size_t index, int next_free) {
        table_[index].next_free = next_free;
        if (std::cmp_equal(next_free, index)) {
            table_[index].data = std::make_unique<T>();
        }
    }

    // Whether the free list runs through free entries only, each once, and
    // the entries in use are counted right.
    [[nodiscard]] bool valid_free_list() const {
        int used = 0;
        for (std::size_t i = 0; i < table_.size(); ++i) {
            const int next = table_[i].next_free;
            if (std::cmp_equal(next, i)) {
                ++used;
            } else if (next < invalid || std::cmp_greater_equal(next, table_.size())) {
                return false;
            }
        }
        if (used != entries_used_) {
            return false;
        }
        std::vector<bool> seen(table_.size(), false);
        for (int index = first_free_; index != invalid; index = table_[static_cast<std::size_t>(index)].next_free) {
            if (index < 0 || std::cmp_greater_equal(index, table_.size()) || is_valid_entry(index) ||
                seen[static_cast<std::size_t>(index)]) {
                return false;
            }
            seen[static_cast<std::size_t>(index)] = true;
        }
        return true;
    }

    [[nodiscard]] T& at(std::size_t index) { return *table_[index].data; }

    [[nodiscard]] const T& at(std::size_t index) const { return *table_[index].data; }

private:
    static constexpr int invalid = -1;

    struct Entry {
        std::unique_ptr<T> data;
        int next_free = invalid; // Equal to the entry's index while it is in use.
    };

    int first_free_ = invalid;
    int entries_used_ = 0;
    std::vector<Entry> table_;
};

class CloneTable final : public Table<Object> {
public:
    CloneTable() : Table<Object>(SegmentType::Clones) {}

    void free_at_address(SegManager& segments, Reg address) override;
    [[nodiscard]] std::vector<Reg> list_all_outgoing_references(Reg address) const override;
};

class NodeTable final : public Table<Node> {
public:
    NodeTable() : Table<Node>(SegmentType::Nodes) {}

    void free_at_address(SegManager& /*segments*/, Reg address) override { free_entry(address.offset); }

    [[nodiscard]] std::vector<Reg> list_all_outgoing_references(Reg address) const override;
};

class ListTable final : public Table<List> {
public:
    ListTable() : Table<List>(SegmentType::Lists) {}

    void free_at_address(SegManager& /*segments*/, Reg address) override { free_entry(address.offset); }

    [[nodiscard]] std::vector<Reg> list_all_outgoing_references(Reg address) const override;
};

class HunkTable final : public Table<Hunk> {
public:
    HunkTable() : Table<Hunk>(SegmentType::Hunk) {}

    void free_entry_contents(int index) {
        Hunk& hunk = at(static_cast<std::size_t>(index));
        hunk.mem.clear();
        hunk.has_memory = false;
    }

    void free_entry(int index) override {
        free_entry_contents(index);
        Table<Hunk>::free_entry(index);
    }

    void free_at_address(SegManager& /*segments*/, Reg address) override { free_entry(address.offset); }
};

class DynMem final : public Segment {
public:
    DynMem() : Segment(SegmentType::DynMem) {}

    [[nodiscard]] bool is_valid_offset(std::uint32_t offset) const override { return offset < buffer.size(); }

    [[nodiscard]] SegmentRef dereference(Reg pointer) override;

    [[nodiscard]] Reg find_canonic_address(const SegManager& /*segments*/, Reg address) const override {
        return make_reg(address.segment, 0);
    }

    [[nodiscard]] std::vector<Reg> list_all_deallocatable(SegmentId id) const override { return {make_reg(id, 0)}; }

    std::vector<std::uint8_t> buffer;
    std::string description;
};

} // namespace sci::vm
