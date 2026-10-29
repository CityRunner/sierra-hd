// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/seg_manager.cpp (SCI0 paths)
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

#include "seg_manager.hpp"

#include <algorithm>
#include <format>

namespace sci::vm {

namespace {

std::string pointer_text(Reg pointer) {
    return std::format("{:04x}:{:04x}", pointer.segment, pointer.offset);
}

// A character of a string held in registers (locals, the stack): two per
// register, little-endian.
char get_char(const SegmentRef& ref, std::size_t offset) {
    if (ref.skip_byte) {
        ++offset;
    }
    const Reg value = ref.regs[offset / 2];
    // Segment 0xFFFF: uninitialised temporaries, harmless past the first
    // two characters.
    if (value.segment != 0 && (value.segment != 0xFFFF || offset <= 1)) {
        warn("Attempt to read character from non-raw data");
    }
    const bool odd = (offset & 1U) != 0;
    return static_cast<char>(odd ? (value.offset >> 8U) : (value.offset & 0xFFU));
}

void set_char(const SegmentRef& ref, std::size_t offset, std::uint8_t value) {
    if (ref.skip_byte) {
        ++offset;
    }
    Reg& reg = ref.regs[offset / 2];
    reg.segment = 0;
    if ((offset & 1U) != 0) {
        reg.offset = static_cast<std::uint16_t>((reg.offset & 0x00FFU) | static_cast<unsigned>(value << 8U));
    } else {
        reg.offset = static_cast<std::uint16_t>((reg.offset & 0xFF00U) | value);
    }
}

// Copies up to n bytes; a string copy stops after the terminator and, when
// bounded, pads the rest with zeros.
void forward_copy(std::span<std::uint8_t> dest, std::span<const std::uint8_t> source, std::size_t n, bool string) {
    const bool zero_pad = string && n != SegManager::unbounded;
    std::size_t i = 0;
    while (n != 0 && i < dest.size()) {
        --n;
        const std::uint8_t byte = i < source.size() ? source[i] : 0;
        dest[i] = byte;
        ++i;
        if (string && byte == 0) {
            break;
        }
    }
    if (zero_pad) {
        while (n != 0 && i < dest.size()) {
            --n;
            dest[i] = 0;
            ++i;
        }
    }
}

} // namespace

SegManager::SegManager(Resources& resources, const ScriptPatcher* patcher, Faults& faults)
    : resources_(&resources), patcher_(patcher), faults_(&faults) {
    heap_.emplace_back();
    create_class_table();
}

void SegManager::reset() {
    for (std::size_t i = 0; i < heap_.size(); ++i) {
        if (heap_[i]) {
            deallocate(static_cast<SegmentId>(i));
        }
    }
    heap_.clear();
    heap_.emplace_back();
    clones_ = 0;
    lists_ = 0;
    nodes_ = 0;
    hunks_ = 0;
    classes_.clear();
    create_class_table();
}

void SegManager::init_sys_strings() {
    // Both system strings live in one segment, as in ScummVM.
    (void)allocate_dynmem(512, "system strings", save_dir_pointer_);
    parser_pointer_ = make_reg(save_dir_pointer_.segment, save_dir_pointer_.offset + 256);
}

void SegManager::after_restore() {
    if (hunks_ != 0) {
        deallocate(hunks_);
        hunks_ = 0;
    }
    init_sys_strings();
}

SegmentId SegManager::find_free_segment() const {
    std::size_t id = 1;
    while (id < heap_.size() && heap_[id]) {
        ++id;
    }
    return static_cast<SegmentId>(id);
}

SegmentId SegManager::alloc_segment(std::unique_ptr<Segment> segment) {
    const SegmentId id = find_free_segment();
    if (id >= heap_.size()) {
        heap_.emplace_back();
    }
    heap_[id] = std::move(segment);
    return id;
}

Script* SegManager::allocate_script(int number, SegmentId& segment) {
    segment = script_segment(number);
    if (segment > 0) {
        return dynamic_cast<Script*>(heap_[segment].get());
    }
    auto script = std::make_unique<Script>();
    Script* result = script.get();
    segment = alloc_segment(std::move(script));
    script_segments_[number] = segment;
    return result;
}

// NOLINTNEXTLINE(misc-no-recursion): a script frees its locals segment too, as in ScummVM.
void SegManager::deallocate(SegmentId id) {
    if (id < 1 || id >= heap_.size() || !heap_[id]) {
        faults_->raise(std::format("Attempt to deallocate an invalid or freed segment {:x}", id));
        return;
    }
    if (const auto* script = dynamic_cast<const Script*>(heap_[id].get())) {
        script_segments_.erase(script->number());
        const SegmentId locals = script->locals_segment();
        // A reloaded script keeps its segment but gets a new locals segment,
        // which may be below its own and already freed.
        if (locals != 0 && locals < heap_.size() && heap_[locals]) {
            deallocate(locals);
        }
    }
    heap_[id].reset();
}

bool SegManager::is_heap_object(Reg pos) const {
    const Object* obj = object(pos);
    if (obj == nullptr || obj->is_freed()) {
        return false;
    }
    const Script* script = script_if_loaded(pos.segment);
    return script == nullptr || !script->is_marked_deleted();
}

Script* SegManager::script(SegmentId id) const {
    Script* result = script_if_loaded(id);
    if (result == nullptr) {
        faults_->raise(std::format("SegManager::getScript(): seg id {:x} is not a loaded script", id));
    }
    return result;
}

Script* SegManager::script_if_loaded(SegmentId id) const {
    if (id < 1 || id >= heap_.size() || !heap_[id]) {
        return nullptr;
    }
    return dynamic_cast<Script*>(heap_[id].get());
}

SegmentId SegManager::find_segment_by_type(SegmentType type) const {
    for (std::size_t i = 0; i < heap_.size(); ++i) {
        if (heap_[i] && heap_[i]->type() == type) {
            return static_cast<SegmentId>(i);
        }
    }
    return 0;
}

Segment* SegManager::segment(SegmentId id) const {
    if (id < 1 || id >= heap_.size()) {
        return nullptr;
    }
    return heap_[id].get();
}

SegmentType SegManager::segment_type(SegmentId id) const {
    const Segment* found = segment(id);
    return found == nullptr ? SegmentType::Invalid : found->type();
}

Segment* SegManager::segment(SegmentId id, SegmentType type) const {
    Segment* found = segment(id);
    return found != nullptr && found->type() == type ? found : nullptr;
}

Object* SegManager::object(Reg pos) const {
    Segment* found = segment(pos.segment);
    if (found == nullptr) {
        return nullptr;
    }
    if (found->type() == SegmentType::Clones) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): the type is checked above.
        auto* clones = static_cast<CloneTable*>(found);
        if (clones->is_valid_entry(pos.offset)) {
            return &clones->at(pos.offset);
        }
        warn("getObject(): Trying to get an invalid object");
        return nullptr;
    }
    if (found->type() == SegmentType::Script) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): the type is checked above.
        auto* owner = static_cast<Script*>(found);
        if (pos.offset <= owner->size() && owner->offset_is_object(pos.offset)) {
            return owner->object_at(pos.offset);
        }
    }
    return nullptr;
}

std::string SegManager::object_name(Reg pos) const {
    const Object* obj = object(pos);
    if (obj == nullptr) {
        return "<no such object>";
    }
    if (obj->name().is_null()) {
        return "<no name>";
    }
    const SegmentRef ref = dereference(obj->name());
    if (!ref.valid() || !ref.is_raw) {
        return "<invalid name>";
    }
    return get_string(obj->name());
}

SegmentId SegManager::script_segment(int number) const {
    const auto it = script_segments_.find(number);
    return it == script_segments_.end() ? 0 : it->second;
}

SegmentId SegManager::script_segment(int number, ScriptLoad load) {
    if ((static_cast<unsigned>(load) & static_cast<unsigned>(ScriptLoad::Load)) != 0) {
        instantiate_script(number);
    }
    const SegmentId id = script_segment(number);
    if (id > 0 && load == ScriptLoad::Lock) {
        if (Script* loaded = script(id)) {
            loaded->increment_lockers();
        }
    }
    return id;
}

DataStack* SegManager::allocate_stack(int size) {
    auto stack = std::make_unique<DataStack>();
    DataStack* result = stack.get();
    (void)alloc_segment(std::move(stack));
    // SSCI fills the stack with 's' in SCI0 late.
    result->entries.assign(static_cast<std::size_t>(size), number('s'));
    return result;
}

template <typename T>
T* SegManager::table(SegmentId& id) {
    if (id == 0) {
        auto owned = std::make_unique<T>();
        T* result = owned.get();
        id = alloc_segment(std::move(owned));
        return result;
    }
    return static_cast<T*>(heap_[id].get());
}

void SegManager::free_hunk_entry(Reg address) const {
    if (address.is_null()) {
        warn("Attempt to free a Hunk from a null address");
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): segment() checks the type.
    auto* hunks = static_cast<HunkTable*>(segment(address.segment, SegmentType::Hunk));
    if (hunks == nullptr) {
        warn(std::format("Attempt to free Hunk from address {}: Invalid segment type", pointer_text(address)));
        return;
    }
    hunks->free_entry_contents(address.offset);
}

Reg SegManager::allocate_hunk_entry(std::string_view type, std::size_t size) {
    auto* hunks = table<HunkTable>(hunks_);
    const int index = hunks->alloc_entry();
    Hunk& hunk = hunks->at(static_cast<std::size_t>(index));
    hunk.mem.assign(size, 0);
    hunk.has_memory = true;
    hunk.type = type;
    return make_reg(hunks_, index);
}

Hunk* SegManager::hunk(Reg address) const {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): segment() checks the type.
    auto* hunks = static_cast<HunkTable*>(segment(address.segment, SegmentType::Hunk));
    if (hunks == nullptr || !hunks->is_valid_entry(address.offset)) {
        return nullptr;
    }
    Hunk* result = &hunks->at(address.offset);
    return result->has_memory ? result : nullptr;
}

Object* SegManager::allocate_clone(Reg& address) {
    auto* clones = table<CloneTable>(clones_);
    const int index = clones->alloc_entry();
    address = make_reg(clones_, index);
    return &clones->at(static_cast<std::size_t>(index));
}

List* SegManager::allocate_list(Reg& address) {
    auto* lists = table<ListTable>(lists_);
    const int index = lists->alloc_entry();
    address = make_reg(lists_, index);
    return &lists->at(static_cast<std::size_t>(index));
}

Node* SegManager::allocate_node(Reg& address) {
    auto* nodes = table<NodeTable>(nodes_);
    const int index = nodes->alloc_entry();
    address = make_reg(nodes_, index);
    return &nodes->at(static_cast<std::size_t>(index));
}

Reg SegManager::new_node(Reg value, Reg key) {
    Reg address;
    Node* node = allocate_node(address);
    node->pred = null_reg;
    node->succ = null_reg;
    node->key = key;
    node->value = value;
    return address;
}

List* SegManager::lookup_list(Reg address) const {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): segment() checks the type.
    auto* lists = static_cast<ListTable*>(segment(address.segment, SegmentType::Lists));
    if (lists == nullptr || !lists->is_valid_entry(address.offset)) {
        faults_->raise(std::format("Attempt to use non-list {} as list", pointer_text(address)));
        return nullptr;
    }
    return &lists->at(address.offset);
}

Node* SegManager::lookup_node(Reg address, bool stop_on_discarded) const {
    if (address.is_null()) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): segment() checks the type.
    auto* nodes = static_cast<NodeTable*>(segment(address.segment, SegmentType::Nodes));
    if (nodes == nullptr) {
        faults_->raise(std::format("Attempt to use non-node {} as list node", pointer_text(address)));
        return nullptr;
    }
    if (!nodes->is_valid_entry(address.offset)) {
        if (stop_on_discarded) {
            faults_->raise(
                std::format("Attempt to use invalid or discarded reference {} as list node", pointer_text(address)));
        }
        return nullptr;
    }
    return &nodes->at(address.offset);
}

std::span<std::uint8_t> SegManager::allocate_dynmem(std::size_t size, std::string_view description, Reg& address) {
    auto dynmem = std::make_unique<DynMem>();
    DynMem* result = dynmem.get();
    const SegmentId id = alloc_segment(std::move(dynmem));
    address = make_reg(id, 0);
    result->buffer.assign(size, 0);
    result->description = description;
    return result->buffer;
}

bool SegManager::free_dynmem(Reg address) {
    if (segment_type(address.segment) != SegmentType::DynMem) {
        return false;
    }
    deallocate(address.segment);
    return true;
}

SegmentRef SegManager::dereference(Reg pointer) const {
    Segment* found = segment(pointer.segment);
    if (found == nullptr) {
        warn(
            std::format("SegManager::dereference(): Attempt to dereference invalid pointer {}", pointer_text(pointer)));
        return {};
    }
    return found->dereference(pointer);
}

std::span<std::uint8_t> SegManager::deref_bulk(Reg pointer, int entries) const {
    const SegmentRef ref = dereference(pointer);
    if (!ref.valid()) {
        return {};
    }
    if (!ref.is_raw) {
        warn(std::format("Dereferencing pointer {} which is not raw, but expected raw", pointer_text(pointer)));
        return {};
    }
    if (entries > ref.max_size) {
        warn(std::format("Trying to dereference pointer {} beyond end of segment", pointer_text(pointer)));
        return {};
    }
    return ref.raw.first(static_cast<std::size_t>(ref.max_size));
}

std::span<Reg> SegManager::deref_regs(Reg pointer, int entries) const {
    const SegmentRef ref = dereference(pointer);
    if (!ref.valid()) {
        return {};
    }
    if (ref.is_raw) {
        warn(std::format("Dereferencing pointer {} which is raw, but expected not raw", pointer_text(pointer)));
        return {};
    }
    if (ref.skip_byte) {
        warn(std::format("Unaligned pointer read: {} expected with word alignment", pointer_text(pointer)));
        return {};
    }
    if (entries * 2 > ref.max_size) {
        warn(std::format("Trying to dereference pointer {} beyond end of segment", pointer_text(pointer)));
        return {};
    }
    return ref.regs;
}

void SegManager::strncpy(Reg dest, std::string_view source, std::size_t n) const {
    const SegmentRef dest_ref = dereference(dest);
    if (!dest_ref.valid()) {
        warn(std::format("Attempt to strncpy to invalid pointer {}", pointer_text(dest)));
        return;
    }
    // A string_view has no terminator; copy as if it had one.
    std::vector<std::uint8_t> bytes(source.begin(), source.end());
    bytes.push_back(0);
    if (dest_ref.is_raw) {
        forward_copy(dest_ref.raw, bytes, n, true);
        return;
    }
    for (std::size_t i = 0; i < n && i < bytes.size(); ++i) {
        set_char(dest_ref, i, bytes[i]);
        if (bytes[i] == 0) {
            break;
        }
    }
    const auto max_size = static_cast<std::size_t>(dest_ref.max_size);
    if (max_size > n) {
        set_char(dest_ref, n, 0);
    }
}

void SegManager::strncpy(Reg dest, Reg source, std::size_t n) const {
    if (source.is_null()) {
        if (n > 0) {
            strcpy(dest, std::string_view{});
        }
        return;
    }
    const SegmentRef dest_ref = dereference(dest);
    const SegmentRef source_ref = dereference(source);
    if (!source_ref.valid()) {
        warn(std::format("Attempt to strncpy from invalid pointer {}", pointer_text(source)));
        if (n > 0) {
            strcpy(dest, std::string_view{});
        }
        return;
    }
    if (!dest_ref.valid()) {
        warn(std::format("Attempt to strncpy to invalid pointer {}", pointer_text(dest)));
        return;
    }
    if (source_ref.is_raw) {
        const std::span<const std::uint8_t> raw = source_ref.raw.first(static_cast<std::size_t>(source_ref.max_size));
        const auto end = std::ranges::find(raw, std::uint8_t{0});
        strncpy(
            dest,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the bytes as characters.
            std::string_view(reinterpret_cast<const char*>(raw.data()), static_cast<std::size_t>(end - raw.begin())),
            n);
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const char c = get_char(source_ref, i);
        if (dest_ref.is_raw) {
            dest_ref.raw[i] = static_cast<std::uint8_t>(c);
        } else {
            set_char(dest_ref, i, static_cast<std::uint8_t>(c));
        }
        if (c == 0) {
            break;
        }
    }
}

void SegManager::memcpy(Reg dest, std::span<const std::uint8_t> source) const {
    const SegmentRef dest_ref = dereference(dest);
    if (!dest_ref.valid()) {
        warn(std::format("Attempt to memcpy to invalid pointer {}", pointer_text(dest)));
        return;
    }
    const auto count = static_cast<int>(source.size());
    if (count > dest_ref.max_size) {
        warn(std::format("Trying to dereference pointer {} beyond end of segment", pointer_text(dest)));
        return;
    }
    if (dest_ref.is_raw) {
        std::ranges::copy(source, dest_ref.raw.begin());
        return;
    }
    for (std::size_t i = 0; i < source.size(); ++i) {
        set_char(dest_ref, i, source[i]);
    }
}

void SegManager::memcpy(Reg dest, Reg source, std::size_t n) const {
    const SegmentRef dest_ref = dereference(dest);
    const SegmentRef source_ref = dereference(source);
    if (!dest_ref.valid() || !source_ref.valid()) {
        warn(std::format("Attempt to memcpy between invalid pointers {} and {}", pointer_text(dest),
                         pointer_text(source)));
        return;
    }
    const auto count = static_cast<int>(n);
    if (count > dest_ref.max_size || count > source_ref.max_size) {
        warn("Trying to dereference pointer beyond end of segment");
        return;
    }
    if (source_ref.is_raw) {
        memcpy(dest, std::span<const std::uint8_t>(source_ref.raw.first(n)));
    } else if (dest_ref.is_raw) {
        memcpy(dest_ref.raw.first(n), source);
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            set_char(dest_ref, i, static_cast<std::uint8_t>(get_char(source_ref, i)));
        }
    }
}

void SegManager::memcpy(std::span<std::uint8_t> dest, Reg source) const {
    const SegmentRef source_ref = dereference(source);
    if (!source_ref.valid()) {
        warn(std::format("Attempt to memcpy from invalid pointer {}", pointer_text(source)));
        return;
    }
    const auto count = static_cast<int>(dest.size());
    if (count > source_ref.max_size) {
        warn(std::format("Trying to dereference pointer {} beyond end of segment", pointer_text(source)));
        return;
    }
    for (std::size_t i = 0; i < dest.size(); ++i) {
        dest[i] = source_ref.is_raw ? source_ref.raw[i] : static_cast<std::uint8_t>(get_char(source_ref, i));
    }
}

std::size_t SegManager::strlen(Reg str) const {
    if (str.is_null()) {
        return 0;
    }
    const SegmentRef ref = dereference(str);
    if (!ref.valid()) {
        warn(std::format("Attempt to call strlen on invalid pointer {}", pointer_text(str)));
        return 0;
    }
    if (ref.is_raw) {
        const auto raw = ref.raw.first(static_cast<std::size_t>(ref.max_size));
        return static_cast<std::size_t>(std::ranges::find(raw, std::uint8_t{0}) - raw.begin());
    }
    std::size_t i = 0;
    while (get_char(ref, i) != 0) {
        ++i;
    }
    return i;
}

std::string SegManager::get_string(Reg pointer) const {
    std::string result;
    if (pointer.is_null()) {
        return result;
    }
    const SegmentRef ref = dereference(pointer);
    if (!ref.valid()) {
        warn(std::format("SegManager::getString(): Attempt to dereference invalid pointer {}", pointer_text(pointer)));
        return result;
    }
    if (ref.is_raw) {
        const auto raw = ref.raw.first(static_cast<std::size_t>(ref.max_size));
        const auto end = std::ranges::find(raw, std::uint8_t{0});
        result.assign(raw.begin(), end);
        return result;
    }
    const auto max_size = static_cast<std::size_t>(ref.max_size);
    for (std::size_t i = 0; i < max_size; ++i) {
        const char c = get_char(ref, i);
        if (c == 0) {
            break;
        }
        result += c;
    }
    return result;
}

void SegManager::create_class_table() {
    const std::vector<std::uint8_t>* vocab = resources_->find(ResourceType::Vocab, 996);
    if (vocab == nullptr) {
        faults_->raise("SegManager: failed to open vocab 996");
        return;
    }
    const std::size_t total = vocab->size() >> 2U;
    classes_.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        classes_[i].reg = null_reg;
        classes_[i].script = static_cast<int>(static_cast<unsigned>((*vocab)[(i * 4) + 2]) |
                                              (static_cast<unsigned>((*vocab)[(i * 4) + 3]) << 8U));
    }
}

Reg SegManager::class_address(int class_number, ScriptLoad load, SegmentId caller_segment) {
    if (class_number == 0xFFFF) {
        return null_reg;
    }
    if (class_number < 0 || static_cast<std::size_t>(class_number) >= classes_.size() ||
        classes_[static_cast<std::size_t>(class_number)].script < 0) {
        faults_->raise(std::format("[VM] Attempt to dereference class {:x}, which doesn't exist (max {:x})",
                                   class_number, classes_.size()));
        return null_reg;
    }
    const Class& klass = classes_[static_cast<std::size_t>(class_number)];
    if (klass.reg.segment == 0) {
        (void)script_segment(klass.script, load);
        // The class table may have been resized while loading.
        const Class& loaded = classes_[static_cast<std::size_t>(class_number)];
        if (loaded.reg.segment == 0) {
            if (load == ScriptLoad::DontLoad) {
                return null_reg;
            }
            faults_->raise(std::format("[VM] Trying to instantiate class {:x} by instantiating script {} failed",
                                       class_number, loaded.script));
            return null_reg;
        }
        return loaded.reg;
    }
    if (caller_segment != klass.reg.segment) {
        if (Script* owner = script(klass.reg.segment)) {
            owner->increment_lockers();
        }
    }
    return klass.reg;
}

SegmentId SegManager::instantiate_script(int number) {
    SegmentId id = script_segment(number);
    Script* loaded = script_if_loaded(id);
    if (loaded != nullptr) {
        if (!loaded->is_marked_deleted()) {
            loaded->increment_lockers();
            return id;
        }
        loaded->free_script(true);
    } else {
        loaded = allocate_script(number, id);
    }
    std::string message;
    if (!loaded->load(number, *resources_, patcher_, message)) {
        faults_->raise(message);
        return 0;
    }
    loaded->initialize_locals(*this);
    loaded->initialize_objects(*this, id);
    return id;
}

// NOLINTNEXTLINE(misc-no-recursion): unloading a script unloads its superclasses' scripts, as in ScummVM.
void SegManager::uninstantiate_script(int number) {
    const SegmentId id = script_segment(number);
    Script* loaded = script_if_loaded(id);
    if (loaded == nullptr || loaded->is_marked_deleted()) {
        return;
    }
    loaded->decrement_lockers();
    if (loaded->lockers() > 0) {
        return;
    }
    for (Class& klass : classes_) {
        if (klass.reg.segment == id) {
            klass.reg = null_reg;
        }
    }
    uninstantiate_script_sci0(number);
    if (loaded->lockers() == 0) {
        // The scripts delete the script themselves later.
        loaded->mark_deleted();
    }
}

// NOLINTNEXTLINE(misc-no-recursion): see uninstantiate_script.
void SegManager::uninstantiate_script_sci0(int number) {
    const SegmentId id = script_segment(number);
    Script* owner = script(id);
    if (owner == nullptr) {
        return;
    }
    // Uninstantiate the superclasses of every object and class.
    std::uint32_t position = 0;
    std::uint32_t length = 0;
    while (true) {
        position += length;
        const std::uint16_t type = owner->read16(position);
        if (type == 0) {
            break;
        }
        length = owner->read16(position + 2);
        if (length == 0) {
            break;
        }
        if (type == static_cast<std::uint16_t>(ScriptBlock::Object) ||
            type == static_cast<std::uint16_t>(ScriptBlock::Class)) {
            const auto super_class = static_cast<std::int16_t>(owner->read16(position + 4 + 8 + 2));
            if (super_class >= 0 && static_cast<std::size_t>(super_class) < classes_.size()) {
                const int super_script = classes_[static_cast<std::size_t>(super_class)].script;
                if (super_script == number) {
                    if (owner->lockers() != 0) {
                        owner->decrement_lockers();
                    }
                } else {
                    uninstantiate_script(super_script);
                }
            }
        }
    }
}

} // namespace sci::vm
