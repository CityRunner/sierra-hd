// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading the VM state back (milestone 8, docs/formats/save-format.md): the
// counterpart of every save_state in state.cpp, in the same order. What the
// stream leaves out is rebuilt as ScummVM's engines/sci/engine/savegame.cpp
// does after a restore: scripts are reloaded from the resources and their
// saved buffers applied, the songs' resources parsed again, the ports and
// the pointers between the parts reconnected. Every count, index and
// reference is checked before it is used, so that a damaged stream ends in
// a message rather than a crash.

#include "events.hpp"
#include "files.hpp"
#include "gfx_animate.hpp"
#include "gfx_controls.hpp"
#include "gfx_cursor.hpp"
#include "gfx_menu.hpp"
#include "gfx_paint.hpp"
#include "gfx_ports.hpp"
#include "gfx_screen.hpp"
#include "graphics.hpp"
#include "machine.hpp"
#include "midi_parser.hpp"
#include "music.hpp"
#include "parser.hpp"
#include "resources.hpp"
#include "script.hpp"
#include "seg_manager.hpp"
#include "segments.hpp"
#include "sound.hpp"
#include "state.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace sci::vm {

// StateReader's checks ------------------------------------------------------

void StateReader::fail(std::string message) {
    if (message_.empty()) {
        message_ = std::move(message);
    }
    failed_ = true;
}

void StateReader::begin(std::string_view name) {
    const std::string found = string();
    if (!failed_ && found != name) {
        fail(std::format(R"(expected the component "{}", found "{}")", name, found));
    }
}

std::size_t StateReader::count(std::size_t element_bytes) {
    const std::size_t value = size();
    if (failed_) {
        return 0;
    }
    if (value > (data_.size() - position_) / std::max<std::size_t>(element_bytes, 1)) {
        fail(std::format("a count of {} is more than the rest of the state holds", value));
        return 0;
    }
    return value;
}

namespace {

Rect read_rect(StateReader& in) {
    Rect rect;
    rect.top = in.i16();
    rect.left = in.i16();
    rect.bottom = in.i16();
    rect.right = in.i16();
    return rect;
}

SciEvent read_event(StateReader& in) {
    SciEvent event;
    event.type = in.u16();
    event.modifiers = in.u16();
    event.character = in.u16();
    event.mouse.x = in.i16();
    event.mouse.y = in.i16();
    return event;
}

// A table's free list and its entries in use (write_table).
template <typename T, typename ReadEntry>
void read_table(StateReader& in, Table<T>& table, ReadEntry read_entry) {
    const std::size_t size = in.count(4);
    const int first_free = in.i32();
    const int used = in.i32();
    table.restore(size, first_free, used);
    for (std::size_t i = 0; i < size && !in.failed(); ++i) {
        const int next = in.i32();
        table.restore_entry(i, next);
        if (std::cmp_equal(next, i)) {
            read_entry(table.at(i));
        }
    }
    if (!in.failed() && !table.valid_free_list()) {
        in.fail("a table's free list is inconsistent");
    }
}

void read_port_fields(StateReader& in, Port& port) {
    port.id = in.u16();
    port.top = in.i16();
    port.left = in.i16();
    port.rect = read_rect(in);
    port.cur_top = in.i16();
    port.cur_left = in.i16();
    port.font_height = in.i16();
    port.font_id = in.i32();
    port.greyed_output = in.boolean();
    port.pen_color = in.i16();
    port.back_color = in.i16();
    port.pen_mode = in.i16();
    port.counter_till_free = in.u16();
}

void read_window_fields(StateReader& in, Window& window) {
    read_port_fields(in, window);
    window.dims = read_rect(in);
    window.restore_rect = read_rect(in);
    window.style = in.u16();
    window.save_screen_mask = in.u16();
    window.saved_visual = in.reg();
    window.saved_priority = in.reg();
    window.title = in.string();
    window.drawn = in.boolean();
}

AnimateEntry read_animate_entry(StateReader& in) {
    AnimateEntry entry;
    entry.given_order = in.i32();
    entry.object = in.reg();
    entry.view = in.i32();
    entry.loop = in.i16();
    entry.cel = in.i16();
    entry.palette = in.i16();
    entry.x = in.i16();
    entry.y = in.i16();
    entry.z = in.i16();
    entry.priority = in.i16();
    entry.signal = in.u16();
    entry.cel_rect = read_rect(in);
    entry.show_bits = in.boolean();
    entry.cast_handle = in.reg();
    return entry;
}

SoundStatus read_status(StateReader& in) {
    switch (in.u8()) {
    case static_cast<std::uint8_t>(SoundStatus::Stopped):
        return SoundStatus::Stopped;
    case static_cast<std::uint8_t>(SoundStatus::Initialized):
        return SoundStatus::Initialized;
    case static_cast<std::uint8_t>(SoundStatus::Paused):
        return SoundStatus::Paused;
    case static_cast<std::uint8_t>(SoundStatus::Playing):
        return SoundStatus::Playing;
    default:
        in.fail("a song has an unknown status");
        return SoundStatus::Stopped;
    }
}

// A song of the play list (Music::save_state), its resource parsed again.
std::unique_ptr<MusicEntry> read_song(StateReader& in, Resources& resources, Music& music) {
    auto entry = std::make_unique<MusicEntry>();
    MusicEntry& sound = *entry;
    sound.sound_object = in.reg();
    const bool has_resource = in.boolean();
    const bool has_digital_sample = has_resource && in.boolean();
    sound.resource_id = in.u16();
    if (has_resource && !in.failed()) {
        const auto* data = resources.find(ResourceType::Sound, sound.resource_id);
        if (data != nullptr) {
            sound.resource = SoundResource::parse(*data, sound.resource_id);
        }
        if (!sound.resource || sound.resource->has_digital_sample != has_digital_sample) {
            in.fail(std::format("sound {} is not in the game files as saved", sound.resource_id));
        }
    }
    sound.time = in.i32();
    sound.data_inc = in.u16();
    sound.ticker = in.u16();
    sound.signal = in.u16();
    sound.priority = in.i16();
    sound.loop = in.u16();
    sound.volume = in.i16();
    sound.hold = in.i16();
    sound.reverb = static_cast<std::int8_t>(in.u8());
    sound.play_bed = in.boolean();
    sound.override_priority = in.boolean();
    sound.pause_counter = in.i16();
    sound.fade_to = in.u8();
    sound.fade_step = in.i16();
    sound.fade_ticker = in.u32();
    sound.fade_ticker_step = in.u32();
    sound.fade_set_volume = in.boolean();
    sound.fade_completed = in.boolean();
    sound.stop_after_fading = in.boolean();
    sound.status = read_status(in);
    if (in.boolean()) {
        sound.parser = std::make_unique<MidiParser>(music);
        sound.parser->load_state(in, sound);
    }
    sound.signal_queue.resize(in.count(2));
    for (std::uint16_t& signal : sound.signal_queue) {
        signal = in.u16();
    }
    return entry;
}

// A map of the screen's size.
std::vector<std::uint8_t> read_map(StateReader& in, std::string_view name) {
    std::vector<std::uint8_t> map = in.bytes();
    if (!in.failed() && map.size() != static_cast<std::size_t>(Screen::width) * Screen::height) {
        in.fail(std::format("the {} map has {} bytes", name, map.size()));
    }
    return map;
}

} // namespace

// The machine and its segments ----------------------------------------------

void Object::load_state(StateReader& in) {
    pos_ = in.reg();
    name_ = in.reg();
    freed_ = in.boolean();
    method_count_ = in.u16();
    base_vars_.resize(in.count(2));
    for (std::uint16_t& selector : base_vars_) {
        selector = in.u16();
    }
    base_method_.resize(in.count(4));
    for (std::uint32_t& entry : base_method_) {
        entry = in.u32();
    }
    variables_.resize(in.count(4));
    for (Reg& variable : variables_) {
        variable = in.reg();
    }
    if (!in.failed() && variables_.size() < 4) {
        in.fail("an object has fewer than four properties");
    }
    if (!in.failed() && base_method_.size() < static_cast<std::size_t>(method_count_) * 2) {
        in.fail("an object has fewer methods than it counts");
    }
}

void Script::load_state(StateReader& in, Resources& resources, const ScriptPatcher* patcher) {
    // ScummVM reloads the script from the resources and copies the saved
    // buffer over it (Script::syncStringHeap); the exports, synonyms and
    // locals block are found in it again.
    const int number = in.i32();
    std::vector<std::uint8_t> buffer = in.bytes();
    if (in.failed()) {
        return;
    }
    if (number < 0 || number > 0xFFFF) {
        in.fail(std::format("script number {} is out of range", number));
        return;
    }
    std::string message;
    if (!load(number, resources, patcher, message)) {
        in.fail(std::format("script {} cannot be loaded: {}", number, message));
        return;
    }
    if (buffer.size() != buffer_.size()) {
        in.fail(std::format("script {} has {} bytes in the save and {} in the game", number, buffer.size(),
                            buffer_.size()));
        return;
    }
    buffer_ = std::move(buffer);
    lockers_ = in.u32();
    marked_deleted_ = in.boolean();
    locals_segment_ = in.u16();
    objects_.clear();
    const std::size_t objects = in.count(4);
    for (std::size_t i = 0; i < objects && !in.failed(); ++i) {
        const std::uint32_t offset = in.u32();
        Object object;
        object.load_state(in);
        if (!in.failed() && offset >= buffer_.size()) {
            in.fail(std::format("script {} has an object beyond its end", number));
        }
        objects_.insert_or_assign(offset, std::move(object));
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): one case per segment type, as in save_state.
void SegManager::load_state(StateReader& in) {
    std::vector<std::unique_ptr<Segment>> heap(in.count(1));
    for (std::size_t id = 0; id < heap.size() && !in.failed(); ++id) {
        const auto type = static_cast<SegmentType>(in.u8());
        switch (type) {
        case SegmentType::Invalid:
            break;
        case SegmentType::Script: {
            auto script = std::make_unique<Script>();
            script->load_state(in, *resources_, patcher_);
            heap[id] = std::move(script);
            break;
        }
        case SegmentType::Locals: {
            auto locals = std::make_unique<LocalVariables>();
            locals->script_id = in.i32();
            locals->locals.resize(in.count(4));
            for (Reg& value : locals->locals) {
                value = in.reg();
            }
            heap[id] = std::move(locals);
            break;
        }
        case SegmentType::Stack: {
            // ScummVM restores the stack's size, not its contents.
            auto stack = std::make_unique<DataStack>();
            const std::size_t size = in.size();
            if (!in.failed() && size != static_cast<std::size_t>(vm_stack_size)) {
                in.fail(std::format("the stack has {} entries", size));
                break;
            }
            stack->entries.assign(size, null_reg);
            heap[id] = std::move(stack);
            break;
        }
        case SegmentType::Clones: {
            auto table = std::make_unique<CloneTable>();
            read_table(in, *table, [&](Object& object) { object.load_state(in); });
            heap[id] = std::move(table);
            break;
        }
        case SegmentType::Lists: {
            auto table = std::make_unique<ListTable>();
            read_table(in, *table, [&](List& list) {
                list.first = in.reg();
                list.last = in.reg();
                if (in.size() != list.next_nodes.size()) {
                    in.fail("a list has the wrong number of iterators");
                }
                for (Reg& node : list.next_nodes) {
                    node = in.reg();
                }
                list.num_recursions = in.i32();
                if (list.num_recursions < 0 || std::cmp_greater(list.num_recursions, list.next_nodes.size())) {
                    in.fail("a list's recursion depth is out of range");
                }
            });
            heap[id] = std::move(table);
            break;
        }
        case SegmentType::Nodes: {
            auto table = std::make_unique<NodeTable>();
            read_table(in, *table, [&](Node& node) {
                node.pred = in.reg();
                node.succ = in.reg();
                node.key = in.reg();
                node.value = in.reg();
            });
            heap[id] = std::move(table);
            break;
        }
        case SegmentType::Hunk: {
            auto table = std::make_unique<HunkTable>();
            read_table(in, *table, [&](Hunk& hunk) {
                hunk.type = in.string();
                hunk.has_memory = in.boolean();
                hunk.mem = in.bytes();
            });
            heap[id] = std::move(table);
            break;
        }
        case SegmentType::DynMem: {
            auto dynmem = std::make_unique<DynMem>();
            dynmem->description = in.string();
            dynmem->buffer = in.bytes();
            heap[id] = std::move(dynmem);
            break;
        }
        default:
            in.fail(std::format("segment {} has the unknown type {}", id, static_cast<int>(type)));
            break;
        }
    }
    std::map<int, SegmentId> script_segments;
    const std::size_t scripts = in.count(6);
    for (std::size_t i = 0; i < scripts && !in.failed(); ++i) {
        const int number = in.i32();
        const SegmentId id = in.u16();
        const auto* script = id < heap.size() && heap[id] && heap[id]->type() == SegmentType::Script
                                 ? static_cast<const Script*>(heap[id].get()) // NOLINT(*-static-cast-downcast)
                                 : nullptr;
        if (!in.failed() && (script == nullptr || script->number() != number)) {
            in.fail(std::format("script {} is not in segment {}", number, id));
        }
        script_segments.emplace(number, id);
    }
    std::vector<Class> classes(in.count(8));
    for (Class& entry : classes) {
        entry.script = in.i32();
        entry.reg = in.reg();
    }
    const auto table_id = [&](SegmentType type) {
        const SegmentId id = in.u16();
        if (!in.failed() && id != 0 && (id >= heap.size() || !heap[id] || heap[id]->type() != type)) {
            in.fail(std::format("segment {} is not of type {}", id, static_cast<int>(type)));
        }
        return id;
    };
    const SegmentId clones = table_id(SegmentType::Clones);
    const SegmentId lists = table_id(SegmentType::Lists);
    const SegmentId nodes = table_id(SegmentType::Nodes);
    const SegmentId hunks = table_id(SegmentType::Hunk);
    const Reg save_dir = in.reg();
    const Reg parser = in.reg();
    // Each script's locals segment, and the block it points to.
    for (const auto& segment : heap) {
        if (in.failed() || !segment || segment->type() != SegmentType::Script) {
            continue;
        }
        auto& script = static_cast<Script&>(*segment); // NOLINT(*-static-cast-downcast): the type tells the class.
        const SegmentId locals = script.locals_segment();
        script.set_locals_block(nullptr);
        if (locals == 0) {
            continue;
        }
        if (locals >= heap.size() || !heap[locals] || heap[locals]->type() != SegmentType::Locals) {
            in.fail(std::format("script {} has no locals in segment {}", script.number(), locals));
            continue;
        }
        script.set_locals_block(static_cast<LocalVariables*>(heap[locals].get())); // NOLINT(*-static-cast-downcast)
    }
    if (in.failed()) {
        return;
    }
    heap_ = std::move(heap);
    script_segments_ = std::move(script_segments);
    classes_ = std::move(classes);
    clones_ = clones;
    lists_ = lists;
    nodes_ = nodes;
    hunks_ = hunks;
    save_dir_pointer_ = save_dir;
    parser_pointer_ = parser;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the execution, checked and taken, then the machine.
void Machine::load_state(StateReader& in, Execution execution) {
    // The execution. A restore in the game re-enters it through its replay
    // method, so the frames, registers and stack contents are read and
    // left and only the counters carry over; a resume takes them all
    // (docs/formats/save-format.md).
    in.begin("execution");
    constexpr std::size_t frame_bytes = 79;
    std::list<ExecStack> frames(in.count(frame_bytes));
    for (ExecStack& frame : frames) {
        frame.objp = in.reg();
        frame.sendp = in.reg();
        frame.pc = in.reg();
        frame.var_object = in.reg();
        frame.var_index = in.i32();
        frame.fp = in.u64();
        frame.sp = in.u64();
        frame.argc = in.i32();
        frame.argp = in.u64();
        frame.temp_count = in.i32();
        frame.local_segment = in.u16();
        frame.debug_selector = in.i32();
        frame.debug_export = in.i32();
        frame.debug_local_call = in.i32();
        frame.debug_origin = in.i32();
        frame.debug_kernel = in.i32();
        frame.debug_kernel_sub = in.i32();
        const std::uint8_t type = in.u8();
        if (type > static_cast<std::uint8_t>(ExecType::VarSelector)) {
            in.fail("a frame of an unknown type");
        } else {
            frame.type = static_cast<ExecType>(type);
        }
    }
    const int xs_index = in.i32();
    const int stack_base = in.i32();
    const bool position_changed = in.boolean();

    struct SavedVariables {
        bool stored = false;
        StackIndex start = 0;
        SegmentId segment = 0;
        int max = 0;
    };

    std::array<SavedVariables, 4> variables{};
    for (SavedVariables& saved : variables) {
        saved.stored = in.boolean();
        saved.start = in.u64();
        saved.segment = in.u16();
        saved.max = in.i32();
    }
    const Reg acc = in.reg();
    const Reg prev = in.reg();
    const int rest = in.i32();
    const std::uint8_t abort = in.u8();
    const std::uint32_t steps = in.u32();
    const std::uint64_t kernel_calls = in.u64();
    std::vector<Reg> stack(in.count(4));
    for (Reg& value : stack) {
        value = in.reg();
    }
    if (execution == Execution::Resume && !in.failed()) {
        if (xs_index < -2 || std::cmp_greater_equal(xs_index, frames.size()) || xs_index == -1 ||
            std::cmp_greater_equal(stack_base, frames.size()) || abort > static_cast<std::uint8_t>(Abort::QuitGame)) {
            in.fail("the execution state is inconsistent");
        }
        // A send that carries the stack pointer over marks it and the frame
        // pointer `carry`.
        const auto beyond = [&](StackIndex index) { return index > stack.size() && index != carry; };
        for (const ExecStack& frame : frames) {
            if (beyond(frame.fp) || beyond(frame.sp) || frame.argp > stack.size()) {
                in.fail("a frame points beyond the stack");
            }
        }
    }
    in.begin("segments");
    if (in.failed()) {
        return;
    }
    segments_.load_state(in);

    in.begin("machine");
    const Reg game_object = in.reg();
    const std::uint8_t restarting = in.u8();
    const int gc_count_down = in.i32();
    const int last_save_virtual_id = in.i32();
    const int last_save_new_id = in.i32();
    if (in.failed()) {
        return;
    }
    if (restarting > static_cast<std::uint8_t>(Restarting::Restore)) {
        in.fail("the restart flag is out of range");
        return;
    }
    if (game_object != game_object_) {
        in.fail("the game object is not where the game files put it");
        return;
    }
    // Reconnect the stack and the globals (EngineState::initGlobals,
    // SegManager::reconstructStack).
    const SegmentId stack_segment = segments_.find_segment_by_type(SegmentType::Stack);
    const Script* script0 = segments_.script_if_loaded(segments_.script_segment(0));
    if (stack_segment == 0 || script0 == nullptr || script0->locals_block() == nullptr || script0->number() != 0) {
        in.fail("the state has no stack or no script 0 with its globals");
        return;
    }
    stack_ = static_cast<DataStack*>(segments_.segment(stack_segment)); // NOLINT(*-static-cast-downcast)
    variables_[var_global] = Variables{
        .store = &script0->locals_block()->locals,
        .start = 0,
        .segment = script0->locals_segment(),
        .max = static_cast<int>(script0->locals_block()->locals.size()),
    };
    steps_ = steps;
    kernel_calls_ = kernel_calls;
    if (execution == Execution::Resume) {
        if (stack.empty() && frames.empty()) {
            // Written before init_game connected the stack.
            stack_ = nullptr;
        } else if (stack.size() != stack_->entries.size()) {
            in.fail("the stack's size differs from the state's stack segment");
            return;
        } else {
            stack_->entries = std::move(stack);
        }
        stack_frames_ = std::move(frames);
        xs_ = nullptr;
        if (xs_index >= 0) {
            xs_ = &*std::next(stack_frames_.begin(), xs_index);
        }
        execution_stack_base_ = stack_base;
        stack_position_changed_ = position_changed;
        // A variable store is the locals of its segment or the stack.
        for (std::size_t i = 0; i < variables.size(); ++i) {
            const SavedVariables& saved = variables[i];
            std::vector<Reg>* store = nullptr;
            if (saved.stored) {
                if (saved.segment == stack_segment && stack_ != nullptr) {
                    store = &stack_->entries;
                } else if (Segment* locals = segments_.segment(saved.segment, SegmentType::Locals)) {
                    store = &static_cast<LocalVariables*>(locals)->locals; // NOLINT(*-static-cast-downcast)
                } else {
                    in.fail("a variable store is neither the stack nor locals");
                    return;
                }
            }
            variables_[i] = Variables{.store = store, .start = saved.start, .segment = saved.segment, .max = saved.max};
        }
        acc_ = acc;
        prev_ = prev;
        rest_ = rest;
        abort_ = static_cast<Abort>(abort);
    }
    game_is_restarting_ = static_cast<Restarting>(restarting);
    gc_count_down_ = gc_count_down;
    last_save_virtual_id_ = last_save_virtual_id;
    last_save_new_id_ = last_save_new_id;
}

// The subsystems -----------------------------------------------------------

void Clock::load_state(StateReader& in) {
    microseconds_ = in.u64();
    wait_start_ = in.u64();
    wait_end_ = in.u64();
    music_due_ = in.u64();
    tempo_ = in.u32();
    last_wait_ = in.u32();
    play_time_start_ = in.u32();
    last_elapsed_read_ = in.u64();
    throttle_last_ = in.u32();
    throttle_trigger_ = in.boolean();
}

void EventQueue::load_state(StateReader& in) {
    queue_.clear();
    const std::size_t events = in.count(10);
    for (std::size_t i = 0; i < events && !in.failed(); ++i) {
        queue_.push_back(read_event(in));
    }
    mouse_.x = in.i16();
    mouse_.y = in.i16();
}

void Files::load_state(StateReader& in) {
    std::vector<Handle> handles(in.count(11));
    for (Handle& handle : handles) {
        handle.open = in.boolean();
        handle.name = in.string();
        if (in.boolean()) {
            handle.in = in.bytes();
        }
        handle.position = in.u64();
        if (in.boolean()) {
            handle.out = in.bytes();
        }
        if (!in.failed() && handle.in && handle.position > handle.in->size()) {
            in.fail("a file is read beyond its end");
        }
    }
    if (!in.failed() && handles.empty()) {
        in.fail("the file table is empty");
    }
    if (!in.failed()) {
        handles_ = std::move(handles);
    }
}

void Parser::load_state(StateReader& in) {
    const int language = in.i32();
    parser_event_ = in.reg();
    parser_valid_ = in.boolean();
    synonyms_.resize(in.count(4));
    for (Synonym& synonym : synonyms_) {
        synonym.replaceant = in.u16();
        synonym.replacement = in.u16();
    }
    const bool has_vocabulary = in.boolean();
    if (!in.failed() && (has_vocabulary != vocabulary_.has_value() || language != vocabulary_language_)) {
        in.fail("the parser's vocabulary differs from the game's");
        return;
    }
    if (!has_vocabulary) {
        return;
    }
    const int pronoun = in.i32();
    std::vector<text::ParseNode> nodes(in.count(13));
    for (text::ParseNode& node : nodes) {
        const std::uint8_t type = in.u8();
        node.value = in.i32();
        node.left = in.i32();
        node.right = in.i32();
        switch (type) {
        case static_cast<std::uint8_t>(text::NodeType::Word):
            node.type = text::NodeType::Word;
            break;
        case static_cast<std::uint8_t>(text::NodeType::Leaf):
            node.type = text::NodeType::Leaf;
            break;
        case static_cast<std::uint8_t>(text::NodeType::Branch):
            node.type = text::NodeType::Branch;
            break;
        default:
            in.fail("a parse tree node has an unknown type");
            break;
        }
        const auto in_tree = [&](int index) { return index >= -1 && std::cmp_less(index, nodes.size()); };
        if (!in_tree(node.left) || !in_tree(node.right)) {
            in.fail("a parse tree node points outside the tree");
        }
    }
    if (!in.failed() && vocabulary_) {
        vocabulary_->restore_parse(std::move(nodes), pronoun);
    }
}

void MidiParser::load_state(StateReader& in, MusicEntry& sound) {
    data_ = in.bytes();
    loaded_ = in.boolean();
    position_.play_time = in.u32();
    position_.play_tick = in.u32();
    position_.last_event_time = in.u32();
    position_.last_event_tick = in.u32();
    const bool tracking = in.boolean();
    const std::uint64_t play_pos = in.u64();
    position_.play_pos = tracking ? std::optional<std::size_t>(play_pos) : std::nullopt;
    position_.sub_last_event_time = in.u32();
    position_.sub_last_event_tick = in.u32();
    position_.running_status = in.u8();
    next_event_.start = in.u64();
    next_event_.delta = in.u32();
    next_event_.event = in.u8();
    next_event_.param1 = in.u8();
    next_event_.param2 = in.u8();
    next_event_.data = in.u64();
    next_event_.length = in.u32();
    timer_rate_ = in.u32();
    psec_per_tick_ = in.u32();
    abort_parse_ = in.boolean();
    jumping_to_tick_ = in.boolean();
    loop_tick_ = in.u32();
    master_volume_ = in.u8();
    volume_ = in.u8();
    reset_on_pause_ = in.boolean();
    for (std::uint16_t& note : active_notes_) {
        note = in.u16();
    }
    for (bool& used : channel_used_) {
        used = in.boolean();
    }
    if (!in.failed() && ((tracking && play_pos > data_.size()) || next_event_.start > data_.size() ||
                         next_event_.data > data_.size() || next_event_.length > data_.size() - next_event_.data)) {
        in.fail("a song's position is beyond its end");
    }
    sound_ = &sound;
}

void Music::load_state(StateReader& in, Resources& resources) {
    // SciMusic::saveLoadWithSerializer and reconstructPlayList: the songs
    // come back with their resources parsed again from the game files and
    // their players where they were.
    const std::uint32_t tempo = in.u32();
    const bool sound_on = in.boolean();
    const std::uint16_t master_volume = in.u16();
    const auto driver_reverb = static_cast<std::int8_t>(in.u8());
    const auto global_reverb = static_cast<std::int8_t>(in.u8());
    const int global_pause = in.i32();
    const bool needs_resume = in.boolean();
    const int time_counter = in.i32();
    const std::uint32_t timer_calls = in.u32();
    const bool in_timer = in.boolean();
    std::vector<std::unique_ptr<MusicEntry>> play_list(in.count(8));
    for (auto& entry : play_list) {
        if (in.failed()) {
            break;
        }
        entry = read_song(in, resources, *this);
    }
    std::vector<QueuedCommand> queued(in.count(8));
    for (QueuedCommand& command : queued) {
        const std::int32_t track = in.i32();
        command.midi = in.u32();
        if (track >= 0 && std::cmp_less(track, play_list.size())) {
            command.track_init = play_list[static_cast<std::size_t>(track)].get();
        } else if (track != -1) {
            in.fail("a queued command names a song not in the list");
        }
    }
    if (in.failed()) {
        return;
    }
    tempo_ = tempo;
    sound_on_ = sound_on;
    master_volume_ = master_volume;
    driver_reverb_ = driver_reverb;
    global_reverb_ = global_reverb;
    global_pause_ = global_pause;
    needs_resume_ = needs_resume;
    time_counter_ = time_counter;
    timer_calls_ = timer_calls;
    in_timer_ = in_timer;
    // The songs before stay aside: ScummVM sets the play switch and the
    // master volume on them, then clears them (after_restore).
    previous_play_list_ = std::move(play_list_);
    play_list_ = std::move(play_list);
    queued_commands_ = std::move(queued);
}

void Screen::load_state(StateReader& in) {
    auto visual = read_map(in, "visual");
    auto priority = read_map(in, "priority");
    auto control = read_map(in, "control");
    auto owner = read_map(in, "owner");
    auto source = read_map(in, "priority source");
    const int pic_not_valid = in.i32();
    if (in.failed()) {
        return;
    }
    visual_ = std::move(visual);
    priority_ = std::move(priority);
    control_ = std::move(control);
    owner_ = std::move(owner);
    source_ = std::move(source);
    pic_not_valid_ = pic_not_valid;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the ports and the references between them.
void Ports::load_state(StateReader& in) {
    std::unique_ptr<Port> wmgr;
    if (in.boolean()) {
        wmgr = std::make_unique<Port>(0);
        read_port_fields(in, *wmgr);
    }
    std::unique_ptr<Port> menu;
    if (in.boolean()) {
        menu = std::make_unique<Port>(0);
        read_port_fields(in, *menu);
    }
    std::map<std::uint16_t, std::unique_ptr<Window>> owned;
    const std::size_t windows = in.count(2);
    for (std::size_t i = 0; i < windows && !in.failed(); ++i) {
        const std::uint16_t id = in.u16();
        auto window = std::make_unique<Window>(id);
        read_window_fields(in, *window);
        if (!in.failed() && window->id != id) {
            in.fail("a window's id differs from its key");
        }
        owned.insert_or_assign(id, std::move(window));
    }
    std::vector<std::unique_ptr<Window>> zombies(in.count(2));
    for (auto& window : zombies) {
        window = std::make_unique<Window>(0);
        read_window_fields(in, *window);
    }
    // A port as Ports::save_state refers to it.
    const auto reference = [&] -> Port* {
        switch (in.u8()) {
        case 0:
            return nullptr;
        case 1:
            if (!wmgr) {
                in.fail("a reference to a missing window manager port");
            }
            return wmgr.get();
        case 2:
            if (!menu) {
                in.fail("a reference to a missing menu port");
            }
            return menu.get();
        case 3: {
            const std::uint16_t id = in.u16();
            const auto found = owned.find(id);
            if (found == owned.end()) {
                in.fail(std::format("a reference to the missing window {}", id));
                return nullptr;
            }
            return found->second.get();
        }
        case 4: {
            const std::size_t index = in.size();
            if (index >= zombies.size()) {
                in.fail("a reference to a missing freed window");
                return nullptr;
            }
            return zombies[index].get();
        }
        default:
            in.fail("a port reference of an unknown kind");
            return nullptr;
        }
    };
    Port* const pic_window = reference();
    Port* const current = reference();
    const Rect menu_bar_rect = read_rect(in);
    const Rect menu_rect = read_rect(in);
    const Rect menu_line = read_rect(in);
    std::list<Port*> visual_order;
    const std::size_t shown = in.count(1);
    for (std::size_t i = 0; i < shown && !in.failed(); ++i) {
        Port* const port = reference();
        if (port == nullptr) {
            in.fail("a null port in the window list");
        }
        visual_order.push_back(port);
    }
    std::vector<Port*> by_id(in.count(1));
    for (Port*& port : by_id) {
        port = reference();
    }
    const std::uint16_t free_counter = in.u16();
    const std::int16_t priority_top = in.i16();
    const std::int16_t priority_bottom = in.i16();
    const std::int16_t band_count = in.i16();
    std::array<std::uint8_t, 200> bands{};
    for (std::uint8_t& band : bands) {
        band = in.u8();
    }
    if (in.failed()) {
        return;
    }
    // The ports the rest of the engine expects, and windows wherever the
    // engine takes a port for a window.
    const auto is_window = [&](const Port* port) {
        return port != nullptr &&
               (std::ranges::any_of(owned, [&](const auto& entry) { return entry.second.get() == port; }) ||
                std::ranges::any_of(zombies, [&](const auto& window) { return window.get() == port; }));
    };
    if (!wmgr || !menu || current == nullptr || by_id.size() < first_script_window_id || by_id[0] != wmgr.get() ||
        by_id[1] != wmgr.get()) {
        in.fail("the ports lack the window manager, the menu port or the current port");
        return;
    }
    for (std::size_t id = first_window_id; id < by_id.size(); ++id) {
        if (by_id[id] != nullptr && (!is_window(by_id[id]) || by_id[id]->id != id)) {
            in.fail(std::format("port {} is not a window of that id", id));
            return;
        }
    }
    if (!std::ranges::all_of(visual_order, [&](const Port* port) { return port == wmgr.get() || is_window(port); })) {
        in.fail("the window list holds a port that is not a window");
        return;
    }
    if (!is_window(pic_window)) {
        in.fail("the picture window is missing");
        return;
    }
    Window* const pic = as_window(pic_window);
    wmgr_port_ = std::move(wmgr);
    menu_port_ = std::move(menu);
    owned_ = std::move(owned);
    zombies_ = std::move(zombies);
    pic_window_ = pic;
    current_ = current;
    menu_bar_rect_ = menu_bar_rect;
    menu_rect_ = menu_rect;
    menu_line_ = menu_line;
    windows_ = std::move(visual_order);
    by_id_ = std::move(by_id);
    free_counter_ = free_counter;
    priority_top_ = priority_top;
    priority_bottom_ = priority_bottom;
    priority_band_count_ = band_count;
    priority_bands_ = bands;
}

void Paint::load_state(StateReader& in) {
    std::vector<std::uint8_t> pairs = in.bytes();
    if (!in.failed() && pairs.size() != maps_.pairs.size()) {
        in.fail(std::format("the colour pairs have {} bytes", pairs.size()));
        return;
    }
    std::vector<ShownPicture> stack(in.count(9));
    for (ShownPicture& picture : stack) {
        picture.number = in.i32();
        picture.mirrored = in.boolean();
        picture.ega_palette = in.i32();
    }
    std::vector<ShownLine> lines(in.count(17));
    for (ShownLine& line : lines) {
        line.x0 = in.i32();
        line.y0 = in.i32();
        line.x1 = in.i32();
        line.y1 = in.i32();
        line.color = in.u8();
    }
    if (in.failed()) {
        return;
    }
    maps_.pairs = std::move(pairs);
    drawn_stack_ = std::move(stack);
    drawn_lines_ = std::move(lines);
}

void Transitions::load_state(StateReader& in) {
    number_ = in.i32();
    blackout_ = in.boolean();
    last_ = in.i32();
}

void Menu::load_state(StateReader& in) {
    std::list<MenuEntry> menus;
    const std::size_t menu_count = in.count(14);
    for (std::size_t i = 0; i < menu_count && !in.failed(); ++i) {
        MenuEntry& menu = menus.emplace_back();
        menu.id = in.u16();
        menu.text = in.string();
        menu.text_split = in.string();
        menu.text_width = in.i32();
    }
    std::list<MenuItemEntry> items;
    const std::size_t item_count = in.count(40);
    for (std::size_t i = 0; i < item_count && !in.failed(); ++i) {
        MenuItemEntry& item = items.emplace_back();
        item.menu_id = in.u16();
        item.id = in.u16();
        item.enabled = in.boolean();
        item.tag = in.u16();
        item.key_press = in.u16();
        item.key_modifier = in.u16();
        item.separator_line = in.boolean();
        item.said = in.reg();
        item.text = in.string();
        item.text_split = in.string();
        item.text_pointer = in.reg();
        item.text_width = in.i32();
        item.text_right_aligned = in.string();
        item.text_right_aligned_width = in.i32();
    }
    const std::uint16_t current_menu = in.u16();
    const std::uint16_t current_item = in.u16();
    const Rect menu_rect = read_rect(in);
    const Reg menu_save = in.reg();
    const Reg bar_save = in.reg();
    const bool has_old_port = in.boolean();
    const std::uint16_t old_port_id = in.u16();
    const bool mouse_old_state = in.boolean();
    if (in.failed()) {
        return;
    }
    Port* old_port = nullptr;
    if (has_old_port) {
        old_port = graphics_->ports().port_by_id(old_port_id);
        if (old_port == nullptr) {
            in.fail(std::format("the menu returns to the missing port {}", old_port_id));
            return;
        }
    }
    list_ = std::move(menus);
    items_ = std::move(items);
    current_menu_ = current_menu;
    current_item_ = current_item;
    menu_rect_ = menu_rect;
    menu_save_handle_ = menu_save;
    bar_save_handle_ = bar_save;
    old_port_ = old_port;
    mouse_old_state_ = mouse_old_state;
}

void Controls::load_state(StateReader& in) {
    cursor_rect_ = read_rect(in);
    cursor_visible_ = in.boolean();
    blink_time_ = in.u32();
}

void Animate::load_state(StateReader& in) {
    std::vector<AnimateEntry> cast(in.count(40));
    for (AnimateEntry& entry : cast) {
        entry = read_animate_entry(in);
    }
    if (!in.failed()) {
        last_cast_ = std::move(cast);
        list_.clear();
    }
}

void Subsystems::load_state(StateReader& in) {
    in.begin("clock");
    clock_->load_state(in);
    screen_update_time_ = in.u32();

    in.begin("random");
    random_->set_seed(in.u32());
    seed_ = in.u32();

    in.begin("events");
    events_->load_state(in);
    event_counter_ = in.i32();

    in.begin("files");
    files_->load_state(in);

    in.begin("parser");
    parser_->load_state(in);

    in.begin("sound");
    const bool has_music = in.boolean();
    if (!in.failed() && has_music != sound_->has_music()) {
        in.fail("the music player is missing");
        return;
    }
    if (has_music) {
        sound_->music().load_state(in, graphics_->machine().resources());
    }

    in.begin("graphics");
    const bool attached = in.boolean();
    if (!in.failed() && attached != graphics_->attached()) {
        in.fail("the graphics are missing");
        return;
    }
    if (!attached || in.failed()) {
        return;
    }
    Graphics& graphics = *graphics_;
    graphics.screen().load_state(in);
    graphics.ports().load_state(in);
    graphics.paint().load_state(in);
    const bool cursor_visible = in.boolean();
    const int cursor_shape = in.i32();
    if (!in.failed() && cursor_shape != -1 &&
        graphics.machine().resources().find(ResourceType::Cursor, cursor_shape) == nullptr) {
        in.fail(std::format("the cursor {} is not in the game files", cursor_shape));
    }
    graphics.cursor().restore(cursor_visible, cursor_shape);
    graphics.transitions().load_state(in);
    graphics.menu().load_state(in);
    graphics.controls().load_state(in);
    graphics.animate().load_state(in);
}

// The whole state ------------------------------------------------------------

bool read_state(Machine& machine, StateReader& in, Execution execution) {
    machine.load_state(in, execution);
    if (!in.failed()) {
        machine.subsystems().load_state(in);
    }
    if (!in.failed() && !in.at_end()) {
        in.fail("the state goes on after its last component");
    }
    if (execution == Execution::Resume && machine.subsystems().sound().has_music()) {
        machine.subsystems().sound().music().drop_previous_play_list();
    }
    return !in.failed();
}

} // namespace sci::vm
