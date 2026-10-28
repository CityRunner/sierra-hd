// SPDX-License-Identifier: GPL-3.0-or-later
//
// The canonical serialisation of the VM state and its hash (milestone 8,
// docs/ASSUMPTIONS.md, "Milestone 8: the VM state"). What a SCI0 save must
// hold follows ScummVM's engines/sci/engine/savegame.cpp; this writes more
// than it (the execution stack, the clock, the graphics state), so that
// equal hashes mean equal futures.

#include "state.hpp"

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
#include "sci/core/sha256.hpp"
#include "script.hpp"
#include "seg_manager.hpp"
#include "segments.hpp"
#include "sound.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <iterator>
#include <memory>

namespace sci::vm {

// StateWriter and StateReader ----------------------------------------------

void StateWriter::begin(std::string_view name) {
    if (!components_.empty()) {
        components_.back().end = bytes_.size();
    }
    // The name opens the component in the stream too, so that moving a
    // field from one component to the next changes the total.
    components_.push_back(Component{.name = std::string(name), .begin = bytes_.size(), .end = 0});
    string(name);
}

void StateWriter::u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value & 0xFFU));
    u8(static_cast<std::uint8_t>(value >> 8U));
}

void StateWriter::u32(std::uint32_t value) {
    u16(static_cast<std::uint16_t>(value & 0xFFFFU));
    u16(static_cast<std::uint16_t>(value >> 16U));
}

void StateWriter::u64(std::uint64_t value) {
    u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
    u32(static_cast<std::uint32_t>(value >> 32U));
}

void StateWriter::bytes(std::span<const std::uint8_t> data) {
    size(data.size());
    bytes_.insert(bytes_.end(), data.begin(), data.end());
}

void StateWriter::string(std::string_view text) {
    size(text.size());
    std::ranges::transform(text, std::back_inserter(bytes_), [](char c) { return static_cast<std::uint8_t>(c); });
}

std::vector<StateWriter::Component> StateWriter::components() const {
    std::vector<Component> result = components_;
    if (!result.empty()) {
        result.back().end = bytes_.size();
    }
    return result;
}

std::span<const std::uint8_t> StateReader::take(std::size_t count) {
    if (failed_ || data_.size() - position_ < count) {
        failed_ = true;
        position_ = data_.size();
        return {};
    }
    const auto result = data_.subspan(position_, count);
    position_ += count;
    return result;
}

std::uint8_t StateReader::u8() {
    const auto data = take(1);
    return data.empty() ? 0 : data[0];
}

std::uint16_t StateReader::u16() {
    const unsigned low = u8();
    return static_cast<std::uint16_t>(low | (static_cast<unsigned>(u8()) << 8U));
}

std::uint32_t StateReader::u32() {
    const std::uint32_t low = u16();
    return low | (static_cast<std::uint32_t>(u16()) << 16U);
}

std::uint64_t StateReader::u64() {
    const std::uint64_t low = u32();
    return low | (static_cast<std::uint64_t>(u32()) << 32U);
}

std::vector<std::uint8_t> StateReader::bytes() {
    const auto data = take(size());
    return {data.begin(), data.end()};
}

std::string StateReader::string() {
    const auto data = take(size());
    std::string result;
    std::ranges::transform(data, std::back_inserter(result), [](std::uint8_t c) { return static_cast<char>(c); });
    return result;
}

namespace {

void write_rect(StateWriter& out, const Rect& rect) {
    out.i16(rect.top);
    out.i16(rect.left);
    out.i16(rect.bottom);
    out.i16(rect.right);
}

void write_event(StateWriter& out, const SciEvent& event) {
    out.u16(event.type);
    out.u16(event.modifiers);
    out.u16(event.character);
    out.i16(event.mouse.x);
    out.i16(event.mouse.y);
}

// A table's free list and its entries in use, in index order.
template <typename T, typename WriteEntry>
void write_table(StateWriter& out, const Table<T>& table, WriteEntry write_entry) {
    out.size(table.size());
    out.i32(table.first_free());
    out.i32(table.entries_used());
    for (std::size_t i = 0; i < table.size(); ++i) {
        out.i32(table.next_free(i));
        if (table.is_valid_entry(static_cast<int>(i))) {
            write_entry(table.at(i));
        }
    }
}

void write_port_fields(StateWriter& out, const Port& port) {
    out.u16(port.id);
    out.i16(port.top);
    out.i16(port.left);
    write_rect(out, port.rect);
    out.i16(port.cur_top);
    out.i16(port.cur_left);
    out.i16(port.font_height);
    out.i32(port.font_id);
    out.boolean(port.greyed_output);
    out.i16(port.pen_color);
    out.i16(port.back_color);
    out.i16(port.pen_mode);
    out.u16(port.counter_till_free);
}

void write_window_fields(StateWriter& out, const Window& window) {
    write_port_fields(out, window);
    write_rect(out, window.dims);
    write_rect(out, window.restore_rect);
    out.u16(window.style);
    out.u16(window.save_screen_mask);
    out.reg(window.saved_visual);
    out.reg(window.saved_priority);
    out.string(window.title);
    out.boolean(window.drawn);
}

void write_animate_entry(StateWriter& out, const AnimateEntry& entry) {
    out.i32(entry.given_order);
    out.reg(entry.object);
    out.i32(entry.view);
    out.i16(entry.loop);
    out.i16(entry.cel);
    out.i16(entry.palette);
    out.i16(entry.x);
    out.i16(entry.y);
    out.i16(entry.z);
    out.i16(entry.priority);
    out.u16(entry.signal);
    write_rect(out, entry.cel_rect);
    out.boolean(entry.show_bits);
    out.reg(entry.cast_handle);
}

} // namespace

// The machine and its segments ----------------------------------------------

void Object::save_state(StateWriter& out) const {
    out.reg(pos_);
    out.reg(name_);
    out.boolean(freed_);
    out.u16(method_count_);
    out.size(base_vars_.size());
    for (const std::uint16_t selector : base_vars_) {
        out.u16(selector);
    }
    out.size(base_method_.size());
    for (const std::uint32_t entry : base_method_) {
        out.u32(entry);
    }
    out.regs(variables_);
}

void Script::save_state(StateWriter& out) const {
    // The buffer is the patched script resource with the strings the game
    // wrote into it; the rest is read from it when loading.
    out.i32(number_);
    out.bytes(buffer_);
    out.u32(lockers_);
    out.boolean(marked_deleted_);
    out.u16(locals_segment_);
    // A map: in offset order.
    out.size(objects_.size());
    for (const auto& [offset, object] : objects_) {
        out.u32(offset);
        object.save_state(out);
    }
}

void SegManager::save_state(StateWriter& out) const {
    out.size(heap_.size());
    for (const auto& segment : heap_) {
        if (!segment) {
            out.u8(static_cast<std::uint8_t>(SegmentType::Invalid));
            continue;
        }
        out.u8(static_cast<std::uint8_t>(segment->type()));
        // NOLINTBEGIN(cppcoreguidelines-pro-type-static-cast-downcast): the type tells the class.
        switch (segment->type()) {
        case SegmentType::Script:
            static_cast<const Script&>(*segment).save_state(out);
            break;
        case SegmentType::Locals: {
            const auto& locals = static_cast<const LocalVariables&>(*segment);
            out.i32(locals.script_id);
            out.regs(locals.locals);
            break;
        }
        case SegmentType::Stack:
            // The contents belong to the execution (Machine::save_state).
            out.size(static_cast<const DataStack&>(*segment).entries.size());
            break;
        case SegmentType::Clones:
            write_table(out, static_cast<const CloneTable&>(*segment),
                        [&](const Object& object) { object.save_state(out); });
            break;
        case SegmentType::Lists:
            write_table(out, static_cast<const ListTable&>(*segment), [&](const List& list) {
                out.reg(list.first);
                out.reg(list.last);
                out.regs(list.next_nodes);
                out.i32(list.num_recursions);
            });
            break;
        case SegmentType::Nodes:
            write_table(out, static_cast<const NodeTable&>(*segment), [&](const Node& node) {
                out.reg(node.pred);
                out.reg(node.succ);
                out.reg(node.key);
                out.reg(node.value);
            });
            break;
        case SegmentType::Hunk:
            write_table(out, static_cast<const HunkTable&>(*segment), [&](const Hunk& hunk) {
                out.string(hunk.type);
                out.boolean(hunk.has_memory);
                out.bytes(hunk.mem);
            });
            break;
        case SegmentType::DynMem: {
            const auto& dynmem = static_cast<const DynMem&>(*segment);
            out.string(dynmem.description);
            out.bytes(dynmem.buffer);
            break;
        }
        case SegmentType::Invalid:
            break;
        }
        // NOLINTEND(cppcoreguidelines-pro-type-static-cast-downcast)
    }
    out.size(script_segments_.size());
    for (const auto& [number, segment] : script_segments_) {
        out.i32(number);
        out.u16(segment);
    }
    out.size(classes_.size());
    for (const Class& entry : classes_) {
        out.i32(entry.script);
        out.reg(entry.reg);
    }
    out.u16(clones_);
    out.u16(lists_);
    out.u16(nodes_);
    out.u16(hunks_);
    out.reg(save_dir_pointer_);
    out.reg(parser_pointer_);
}

void Machine::save_state(StateWriter& out) const {
    // Where the interpreter is: the frames, the registers and the stack.
    out.begin("execution");
    out.size(stack_frames_.size());
    int xs_index = -1;
    int index = 0;
    for (const ExecStack& frame : stack_frames_) {
        if (&frame == xs_) {
            xs_index = index;
        }
        ++index;
        out.reg(frame.objp);
        out.reg(frame.sendp);
        out.reg(frame.pc);
        out.reg(frame.var_object);
        out.i32(frame.var_index);
        out.u64(frame.fp);
        out.u64(frame.sp);
        out.i32(frame.argc);
        out.u64(frame.argp);
        out.i32(frame.temp_count);
        out.u16(frame.local_segment);
        // The debug fields choose workarounds (find_workaround).
        out.i32(frame.debug_selector);
        out.i32(frame.debug_export);
        out.i32(frame.debug_local_call);
        out.i32(frame.debug_origin);
        out.i32(frame.debug_kernel);
        out.i32(frame.debug_kernel_sub);
        out.u8(static_cast<std::uint8_t>(frame.type));
    }
    out.i32(xs_ == nullptr ? -2 : xs_index);
    out.i32(execution_stack_base_);
    out.boolean(stack_position_changed_);
    for (const Variables& variables : variables_) {
        // The store is the locals of `segment` or the stack.
        out.boolean(variables.store != nullptr);
        out.u64(variables.start);
        out.u16(variables.segment);
        out.i32(variables.max);
    }
    out.reg(acc_);
    out.reg(prev_);
    out.i32(rest_);
    out.u8(static_cast<std::uint8_t>(abort_));
    out.u32(steps_);
    out.u64(kernel_calls_);
    // The whole stack: uninitialised temporaries read what calls left there.
    if (stack_ != nullptr) {
        out.regs(stack_->entries);
    } else {
        out.size(0);
    }

    out.begin("segments");
    segments_.save_state(out);

    out.begin("machine");
    out.reg(game_object_);
    out.boolean(game_is_restarting_);
    out.i32(gc_count_down_);
}

// The subsystems -----------------------------------------------------------

void Clock::save_state(StateWriter& out) const {
    out.u64(microseconds_);
    out.u64(wait_start_);
    out.u64(wait_end_);
    out.u64(music_due_);
    out.u32(tempo_);
    out.u32(last_wait_);
    out.u64(last_elapsed_read_);
    out.u32(throttle_last_);
    out.boolean(throttle_trigger_);
}

void EventQueue::save_state(StateWriter& out) const {
    out.size(queue_.size());
    for (const SciEvent& event : queue_) {
        write_event(out, event);
    }
    out.i16(mouse_.x);
    out.i16(mouse_.y);
}

void Files::save_state(StateWriter& out) const {
    out.size(handles_.size());
    for (const Handle& handle : handles_) {
        out.boolean(handle.open);
        out.string(handle.name);
        out.boolean(handle.in.has_value());
        if (handle.in) {
            out.bytes(*handle.in);
        }
        out.u64(handle.position);
        out.boolean(handle.out.has_value());
        if (handle.out) {
            out.bytes(*handle.out);
        }
    }
}

void Parser::save_state(StateWriter& out) const {
    out.i32(vocabulary_language_);
    out.reg(parser_event_);
    out.boolean(parser_valid_);
    out.size(synonyms_.size());
    for (const Synonym& synonym : synonyms_) {
        out.u16(synonym.replaceant);
        out.u16(synonym.replacement);
    }
    // The vocabulary is read from the resources; what the last parse left
    // (its tree, for kSaid, and the pronoun's word) is not.
    out.boolean(vocabulary_.has_value());
    if (vocabulary_) {
        out.i32(vocabulary_->pronoun_reference());
        const auto nodes = vocabulary_->nodes();
        out.size(nodes.size());
        for (const text::ParseNode& node : nodes) {
            out.u8(static_cast<std::uint8_t>(node.type));
            out.i32(node.value);
            out.i32(node.left);
            out.i32(node.right);
        }
    }
}

void MidiParser::save_state(StateWriter& out) const {
    out.bytes(data_);
    out.boolean(loaded_);
    out.u32(position_.play_time);
    out.u32(position_.play_tick);
    out.u32(position_.last_event_time);
    out.u32(position_.last_event_tick);
    out.boolean(position_.play_pos.has_value());
    out.u64(position_.play_pos.value_or(0));
    out.u32(position_.sub_last_event_time);
    out.u32(position_.sub_last_event_tick);
    out.u8(position_.running_status);
    out.u64(next_event_.start);
    out.u32(next_event_.delta);
    out.u8(next_event_.event);
    out.u8(next_event_.param1);
    out.u8(next_event_.param2);
    out.u64(next_event_.data);
    out.u32(next_event_.length);
    out.u32(timer_rate_);
    out.u32(psec_per_tick_);
    out.boolean(abort_parse_);
    out.boolean(jumping_to_tick_);
    out.u32(loop_tick_);
    out.u8(master_volume_);
    out.u8(volume_);
    out.boolean(reset_on_pause_);
    for (const std::uint16_t note : active_notes_) {
        out.u16(note);
    }
    for (const bool used : channel_used_) {
        out.boolean(used);
    }
}

void Music::save_state(StateWriter& out) const {
    out.u32(tempo_);
    out.boolean(sound_on_);
    out.u16(master_volume_);
    out.u8(static_cast<std::uint8_t>(driver_reverb_));
    out.u8(static_cast<std::uint8_t>(global_reverb_));
    out.i32(global_pause_);
    out.boolean(needs_resume_);
    out.i32(time_counter_);
    out.u32(timer_calls_);
    out.boolean(in_timer_);
    // In play order (sorted by priority, then by time added).
    out.size(play_list_.size());
    for (const auto& entry : play_list_) {
        const MusicEntry& sound = *entry;
        out.reg(sound.sound_object);
        out.boolean(sound.resource.has_value());
        if (sound.resource) {
            // The data is the resource's; only what was found in it.
            out.boolean(sound.resource->has_digital_sample);
        }
        out.u16(sound.resource_id);
        out.i32(sound.time);
        out.u16(sound.data_inc);
        out.u16(sound.ticker);
        out.u16(sound.signal);
        out.i16(sound.priority);
        out.u16(sound.loop);
        out.i16(sound.volume);
        out.i16(sound.hold);
        out.u8(static_cast<std::uint8_t>(sound.reverb));
        out.boolean(sound.play_bed);
        out.boolean(sound.override_priority);
        out.i16(sound.pause_counter);
        out.u8(sound.fade_to);
        out.i16(sound.fade_step);
        out.u32(sound.fade_ticker);
        out.u32(sound.fade_ticker_step);
        out.boolean(sound.fade_set_volume);
        out.boolean(sound.fade_completed);
        out.boolean(sound.stop_after_fading);
        out.u8(static_cast<std::uint8_t>(sound.status));
        out.boolean(sound.parser != nullptr);
        if (sound.parser) {
            sound.parser->save_state(out);
        }
        out.size(sound.signal_queue.size());
        for (const std::uint16_t signal : sound.signal_queue) {
            out.u16(signal);
        }
    }
    out.size(queued_commands_.size());
    for (const QueuedCommand& command : queued_commands_) {
        // The track to initialise, by its place in the play list.
        const auto found = std::ranges::find_if(
            play_list_, [&](const std::unique_ptr<MusicEntry>& entry) { return entry.get() == command.track_init; });
        out.i32(command.track_init == nullptr ? -1 : static_cast<std::int32_t>(found - play_list_.begin()));
        out.u32(command.midi);
    }
}

void Screen::save_state(StateWriter& out) const {
    // The 1x maps scripts draw into and read (kOnControl, kCanBeHere, the
    // saved bits). What was shown and the HD bookkeeping are not state.
    out.bytes(visual_);
    out.bytes(priority_);
    out.bytes(control_);
    out.i32(pic_not_valid_);
}

void Ports::save_state(StateWriter& out) const {
    // A port by what owns it: the window manager's, the menu's, a window by
    // its id, or a freed window still current.
    const auto reference = [&](const Port* port) {
        if (port == nullptr) {
            out.u8(0);
        } else if (port == wmgr_port_.get()) {
            out.u8(1);
        } else if (port == menu_port_.get()) {
            out.u8(2);
        } else if (const auto zombie = std::ranges::find_if(
                       zombies_, [&](const std::unique_ptr<Window>& window) { return window.get() == port; });
                   zombie != zombies_.end()) {
            out.u8(4);
            out.size(static_cast<std::size_t>(zombie - zombies_.begin()));
        } else {
            out.u8(3);
            out.u16(port->id);
        }
    };
    out.boolean(wmgr_port_ != nullptr);
    if (wmgr_port_) {
        write_port_fields(out, *wmgr_port_);
    }
    out.boolean(menu_port_ != nullptr);
    if (menu_port_) {
        write_port_fields(out, *menu_port_);
    }
    out.size(owned_.size());
    for (const auto& [id, window] : owned_) {
        out.u16(id);
        write_window_fields(out, *window);
    }
    out.size(zombies_.size());
    for (const auto& window : zombies_) {
        write_window_fields(out, *window);
    }
    reference(pic_window_);
    reference(current_);
    write_rect(out, menu_bar_rect_);
    write_rect(out, menu_rect_);
    write_rect(out, menu_line_);
    out.size(windows_.size());
    for (const Port* port : windows_) {
        reference(port);
    }
    out.size(by_id_.size());
    for (const Port* port : by_id_) {
        reference(port);
    }
    out.u16(free_counter_);
    out.i16(priority_top_);
    out.i16(priority_bottom_);
    out.i16(priority_band_count_);
    for (const std::uint8_t band : priority_bands_) {
        out.u8(band);
    }
}

void Paint::save_state(StateWriter& out) const {
    // The colour pairs carry over to pictures added later; the other maps
    // are copied from the screen before each picture.
    out.bytes(maps_.pairs);
    // The picture stack and the lines drawn into it, from which the HD
    // picture is rebuilt.
    out.size(drawn_stack_.size());
    for (const ShownPicture& picture : drawn_stack_) {
        out.i32(picture.number);
        out.boolean(picture.mirrored);
        out.i32(picture.ega_palette);
    }
    out.size(drawn_lines_.size());
    for (const ShownLine& line : drawn_lines_) {
        out.i32(line.x0);
        out.i32(line.y0);
        out.i32(line.x1);
        out.i32(line.y1);
        out.u8(line.color);
    }
}

void Transitions::save_state(StateWriter& out) const {
    out.i32(number_);
    out.boolean(blackout_);
    out.i32(last_);
}

void Menu::save_state(StateWriter& out) const {
    out.size(list_.size());
    for (const MenuEntry& menu : list_) {
        out.u16(menu.id);
        out.string(menu.text);
        out.string(menu.text_split);
        out.i32(menu.text_width);
    }
    out.size(items_.size());
    for (const MenuItemEntry& item : items_) {
        out.u16(item.menu_id);
        out.u16(item.id);
        out.boolean(item.enabled);
        out.u16(item.tag);
        out.u16(item.key_press);
        out.u16(item.key_modifier);
        out.boolean(item.separator_line);
        out.reg(item.said);
        out.string(item.text);
        out.string(item.text_split);
        out.reg(item.text_pointer);
        out.i32(item.text_width);
        out.string(item.text_right_aligned);
        out.i32(item.text_right_aligned_width);
    }
    out.u16(current_menu_);
    out.u16(current_item_);
    write_rect(out, menu_rect_);
    out.reg(menu_save_handle_);
    out.reg(bar_save_handle_);
    out.boolean(old_port_ != nullptr);
    out.u16(old_port_ != nullptr ? old_port_->id : 0);
    out.boolean(mouse_old_state_);
}

void Controls::save_state(StateWriter& out) const {
    write_rect(out, cursor_rect_);
    out.boolean(cursor_visible_);
    out.u32(blink_time_);
}

void Animate::save_state(StateWriter& out) const {
    // The last cast, which windows removed with reanimation redraw. list_
    // is rebuilt by every kAnimate; drawn_cast_ is only reported.
    out.size(last_cast_.size());
    for (const AnimateEntry& entry : last_cast_) {
        write_animate_entry(out, entry);
    }
}

void Subsystems::save_state(StateWriter& out) const {
    out.begin("clock");
    clock_->save_state(out);
    out.u32(screen_update_time_);

    out.begin("random");
    out.u32(random_->seed());
    out.u32(seed_); // For kRestartGame.

    out.begin("events");
    events_->save_state(out);
    out.i32(event_counter_);

    out.begin("files");
    files_->save_state(out);

    out.begin("parser");
    parser_->save_state(out);

    out.begin("sound");
    const Music* const music = sound_->has_music() ? &sound_->music() : nullptr;
    out.boolean(music != nullptr);
    if (music != nullptr) {
        music->save_state(out);
    }

    out.begin("graphics");
    Graphics& graphics = *graphics_;
    out.boolean(graphics.attached());
    if (graphics.attached()) {
        graphics.screen().save_state(out);
        graphics.ports().save_state(out);
        graphics.paint().save_state(out);
        const Cursor& cursor = graphics.cursor();
        out.boolean(cursor.visible());
        out.i32(cursor.shape());
        graphics.transitions().save_state(out);
        graphics.menu().save_state(out);
        graphics.controls().save_state(out);
        graphics.animate().save_state(out);
    }
}

// The whole state ------------------------------------------------------------

void write_state(Machine& machine, StateWriter& out) {
    machine.save_state(out);
    machine.subsystems().save_state(out);
}

StateHash state_hash(Machine& machine) {
    StateWriter out;
    write_state(machine, out);
    const std::span<const std::uint8_t> data = out.data();
    StateHash result;
    core::Sha256 persistent;
    for (const StateWriter::Component& component : out.components()) {
        const auto bytes = data.subspan(component.begin, component.end - component.begin);
        result.components.push_back(
            StateComponentHash{.name = component.name, .digest = core::sha256(bytes), .bytes = bytes.size()});
        if (component.name != "execution") {
            persistent.update(bytes);
        }
    }
    result.total = core::sha256(data);
    result.persistent = persistent.finish();
    return result;
}

} // namespace sci::vm
