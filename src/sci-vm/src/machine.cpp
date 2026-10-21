// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/vm.cpp, engines/sci/engine/vm_types.cpp,
//   engines/sci/engine/selector.cpp, engines/sci/engine/state.cpp,
//   engines/sci/engine/workarounds.cpp (trackOriginAndFindWorkaround),
//   engines/sci/sci.cpp (initGame, runGame, initStackBaseWithSelector)
//   (SCI0 late paths)
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

#include "machine.hpp"

#include "subsystems.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

namespace sci::vm {

namespace {

// Operand formats of the opcodes (engines/sci/engine/kernel_tables.h,
// g_base_opcode_formats; SCI0 needs no adjustment).
enum class Operand : std::uint8_t {
    None,
    Byte,
    SByte,
    Word,
    SWord,
    Variable,  // Byte or word, by the opcode's low bit.
    SVariable, // Signed byte or word.
    Relative,  // Signed byte or word.
    End,
    Invalid,
};

using OperandFormat = std::array<Operand, 3>;

constexpr OperandFormat var1{Operand::Variable};
constexpr OperandFormat svar1{Operand::SVariable};
constexpr OperandFormat rel1{Operand::Relative};

constexpr std::array<OperandFormat, 128> opcode_formats = [] {
    std::array<OperandFormat, 128> f{};
    f[0x17] = rel1;                                                   // bt
    f[0x18] = rel1;                                                   // bnt
    f[0x19] = rel1;                                                   // jmp
    f[0x1a] = svar1;                                                  // ldi
    f[0x1c] = svar1;                                                  // pushi
    f[0x1f] = var1;                                                   // link
    f[0x20] = {Operand::Relative, Operand::Byte};                     // call
    f[0x21] = {Operand::Variable, Operand::Byte};                     // callk
    f[0x22] = {Operand::Variable, Operand::Byte};                     // callb
    f[0x23] = {Operand::Variable, Operand::SVariable, Operand::Byte}; // calle
    f[0x24] = {Operand::End};                                         // ret
    f[0x25] = {Operand::Byte};                                        // send
    f[0x26] = {Operand::Invalid};                                     // info (SCI3)
    f[0x27] = {Operand::Invalid};                                     // superP (SCI3)
    f[0x28] = var1;                                                   // class
    f[0x29] = {Operand::Invalid};                                     // dummy
    f[0x2a] = {Operand::Byte};                                        // self
    f[0x2b] = {Operand::Variable, Operand::Byte};                     // super
    f[0x2c] = svar1;                                                  // rest
    f[0x2d] = {Operand::SVariable, Operand::Variable};                // lea
    f[0x2f] = {Operand::Invalid};                                     // dummy
    for (int op = 0x31; op <= 0x38; ++op) {
        f[static_cast<std::size_t>(op)] = var1; // Properties.
    }
    f[0x39] = rel1;            // lofsa
    f[0x3a] = rel1;            // lofss
    f[0x3f] = {Operand::Word}; // line
    for (std::size_t op = 0x40; op < 0x80; ++op) {
        f[op] = var1; // Variables.
    }
    return f;
}();

constexpr int op_push_self = 0x3e;

struct Instruction {
    std::uint8_t ext = 0; // The opcode byte; bit 0 selects byte operands.
    std::array<std::int16_t, 4> params{};
    std::uint32_t length = 0;
};

// `value >> 1` on a signed value, as ScummVM shifts its int16 operands: the
// arithmetic shift rounds towards negative infinity.
constexpr int half_floor(int value) {
    return value >= 0 ? value / 2 : -((1 - value) / 2);
}

// Reads one instruction (readPMachineInstruction). Bytes past the end of
// the script read as 0.
Instruction read_instruction(std::span<const std::uint8_t> code, std::uint32_t pc) {
    const auto byte_at = [&](std::uint32_t offset) -> std::uint8_t { return offset < code.size() ? code[offset] : 0; };
    const auto word_at = [&](std::uint32_t offset) -> std::uint16_t {
        return static_cast<std::uint16_t>(static_cast<unsigned>(byte_at(offset)) |
                                          (static_cast<unsigned>(byte_at(offset + 1)) << 8U));
    };
    // A byte as a signed value, as a cast to int8 does.
    const auto signed_byte_at = [&](std::uint32_t offset) -> std::int16_t {
        const int value = byte_at(offset);
        return static_cast<std::int16_t>(value >= 0x80 ? value - 0x100 : value);
    };
    Instruction instruction;
    std::uint32_t offset = pc;
    instruction.ext = byte_at(offset++);
    const std::size_t opcode = instruction.ext >> 1U;
    const bool byte_operands = (instruction.ext & 1U) != 0;
    for (std::size_t i = 0; i < 3; ++i) {
        switch (opcode_formats[opcode][i]) {
        case Operand::Byte:
            instruction.params[i] = byte_at(offset++);
            break;
        case Operand::SByte:
            instruction.params[i] = signed_byte_at(offset++);
            break;
        case Operand::Word:
        case Operand::SWord:
            instruction.params[i] = static_cast<std::int16_t>(word_at(offset));
            offset += 2;
            break;
        case Operand::Variable:
            if (byte_operands) {
                instruction.params[i] = byte_at(offset++);
            } else {
                instruction.params[i] = static_cast<std::int16_t>(word_at(offset));
                offset += 2;
            }
            break;
        case Operand::SVariable:
        case Operand::Relative:
            if (byte_operands) {
                instruction.params[i] = signed_byte_at(offset++);
            } else {
                instruction.params[i] = static_cast<std::int16_t>(word_at(offset));
                offset += 2;
            }
            break;
        case Operand::None:
        case Operand::End:
        case Operand::Invalid:
            break;
        }
    }
    // pushSelf with the low bit set is the debug opcode "file", followed by
    // a zero-terminated file name.
    if (opcode == op_push_self && byte_operands) {
        while (true) {
            const std::uint8_t c = byte_at(offset);
            ++offset;
            if (c == 0 || offset >= code.size()) {
                break;
            }
        }
    }
    instruction.length = offset - pc;
    return instruction;
}

std::string reg_text(Reg reg) {
    return std::format("{:04x}:{:04x}", reg.segment, reg.offset);
}

} // namespace

Machine::Machine(const resource::ResourceManager& resources, Subsystems& subsystems)
    : resources_(resources), segments_(resources_, &patcher_, faults_), subsystems_(&subsystems) {
    if (const auto* vocab = resources_.find(ResourceType::Vocab, 997)) {
        if (!selectors_.load(*vocab)) {
            faults_.raise("Kernel: Could not retrieve selector names");
        }
    } else {
        faults_.raise("Kernel: Could not retrieve selector names");
    }
    kernel_.init(selectors_.cache().cant_be_here != no_selector);
    // The patcher finds the selectors its patches name by the game's names.
    patcher_.attach([this](std::string_view name) { return selectors_.find(name); },
                    [this](std::string message) { faults_.raise(std::move(message)); });
    // The game object: export 0 of script 0 (ResourceManager::findGameObject).
    if (const auto* script0 = resources_.find(ResourceType::Script, 0)) {
        Script probe;
        std::string message;
        if (probe.load(0, resources_, nullptr, message)) {
            if (const auto offset = probe.export_offset(0)) {
                game_object_ = make_reg(1, static_cast<int>(*offset));
            }
        }
        (void)script0;
    }
}

Machine::~Machine() = default;

bool Machine::init_game() {
    // Script 0 must get segment 1.
    const SegmentId script0 = segments_.script_segment(0, ScriptLoad::Lock);
    stack_ = segments_.allocate_stack(vm_stack_size);
    gc_count_down_ = gc_interval - 1;
    if (script0 != 1 || faults_.raised()) {
        faults_.raise("Failed to instantiate script 0");
        return false;
    }
    const Script* script = segments_.script(1);
    if (script == nullptr || script->locals_count() == 0) {
        faults_.raise("Script 0 has no locals block");
        return false;
    }
    variables_[var_global] = Variables{
        .store = &script->locals_block()->locals,
        .start = 0,
        .segment = script->locals_segment(),
        .max = script->locals_count(),
    };
    segments_.init_sys_strings();
    acc_ = null_reg;
    prev_ = null_reg;
    stack_frames_.clear();
    execution_stack_base_ = -1;
    stack_position_changed_ = false;
    abort_ = Abort::None;
    game_is_restarting_ = false;
    if (segments_.instantiate_script(0) == 0) {
        faults_.raise("initGame(): Could not instantiate script 0");
        return false;
    }
    subsystems_->on_init_game(*this);
    // The game language: SCI0 games without one get the release's language,
    // English (SciEngine::setSciLanguage).
    if (selectors_.cache().print_lang != no_selector) {
        const Reg language = read_selector(game_object_, selectors_.cache().print_lang);
        write_selector(game_object_, selectors_.cache().print_lang, language.is_null() ? number(1) : language);
    }
    return !faults_.raised();
}

void Machine::init_stack_base_with_selector(Selector selector) {
    stack_->entries[0] = number(selector);
    stack_->entries[1] = null_reg;
    if (send_selector(game_object_, game_object_, 0, 2, 0) == nullptr) {
        faults_.raise("initStackBaseWithSelector: error while registering the first selector in the call stack");
    }
}

void Machine::run_game() {
    init_stack_base_with_selector(selectors_.cache().play);
    while (!faults_.raised()) {
        stack_position_changed_ = false;
        run_vm();
        if (faults_.raised()) {
            break;
        }
        if (abort_ != Abort::LoadGame) {
            subsystems_->on_stop_game(*this);
        }
        stack_frames_.clear();
        xs_ = nullptr;
        if (abort_ == Abort::RestartGame) {
            segments_.reset();
            if (!init_game()) {
                break;
            }
            init_stack_base_with_selector(selectors_.cache().play);
            game_is_restarting_ = true;
            subsystems_->on_restart_game(*this);
            abort_ = Abort::None;
        } else {
            break;
        }
    }
}

Reg Machine::global(std::size_t index) const {
    const Variables& globals = variables_[var_global];
    if (globals.store == nullptr || std::cmp_greater_equal(index, globals.max)) {
        return null_reg;
    }
    return (*globals.store)[globals.start + index];
}

void Machine::set_global(std::size_t index, Reg value) {
    const Variables& globals = variables_[var_global];
    if (globals.store != nullptr && std::cmp_less(index, globals.max)) {
        (*globals.store)[globals.start + index] = value;
    }
}

std::uint16_t Machine::current_room_number() const {
    return global(global_new_room_no).to_uint16();
}

// Arithmetic -----------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity): ScummVM's call origin and workaround search.
WorkaroundSolution Machine::find_workaround(int index, std::span<const WorkaroundEntry> list) {
    if (xs_ == nullptr) {
        return {};
    }
    // The origin of the call (EngineState::getCurrentCallOrigin).
    const ExecStack& call = *xs_;
    std::string object_name = segments_.object_name(call.sendp);
    std::string method_name;
    const Script* local = segments_.script_if_loaded(call.local_segment);
    const int script_number = local != nullptr ? local->number() : -1;
    Selector selector = call.debug_selector;
    int export_id = call.debug_export;
    if (call.debug_local_call != -1) {
        for (const ExecStack& frame : std::views::reverse(stack_frames_)) {
            if (frame.debug_selector != -1 || frame.debug_export != -1) {
                selector = frame.debug_selector;
                export_id = frame.debug_export;
                break;
            }
        }
    }
    if (call.type == ExecType::Call) {
        if (selector != -1) {
            method_name = selectors_.name(selector);
        } else if (export_id != -1) {
            object_name.clear();
            method_name = std::format("export {}", export_id);
        }
    }
    const int room = current_room_number();
    int inheritance = 0;
    Reg search_object = call.sendp;
    std::string search_name = object_name;
    while (true) {
        for (const WorkaroundEntry& entry : list) {
            const bool object_matches = entry.object == nullptr || entry.object == search_name;
            if ((entry.script == -1 || entry.script == script_number) && (entry.room == -1 || entry.room == room) &&
                (entry.inheritance_level == -1 || entry.inheritance_level == inheritance) && object_matches &&
                entry.method == method_name &&
                (entry.from_index == -1 || (entry.from_index <= index && entry.to_index >= index)) &&
                call.debug_local_call < 0) {
                return entry.solution;
            }
        }
        const Object* obj = segments_.object(search_object);
        if (obj == nullptr) {
            break;
        }
        search_object = obj->super_class();
        if (search_object.is_null()) {
            break;
        }
        search_name = segments_.object_name(search_object);
        ++inheritance;
    }
    return {};
}

Reg Machine::arithmetic_workaround(Reg left, Reg right, const char* operation) {
    const WorkaroundSolution solution = find_workaround(0, arithmetic_workarounds());
    if (solution.type == WorkaroundType::None) {
        faults_.raise(std::format("Invalid arithmetic operation ({} - params: {} and {})", operation, reg_text(left),
                                  reg_text(right)));
        return null_reg;
    }
    return number(solution.value);
}

Reg Machine::add(Reg left, Reg right) {
    // A number plus a pointer is the pointer plus the number.
    if (left.is_number() && right.is_pointer()) {
        std::swap(left, right);
    }
    if (left.is_pointer() && right.is_number()) {
        const Segment* segment = segments_.segment(left.segment);
        if (segment == nullptr) {
            faults_.raise(std::format("[VM]: Attempt to add {} to invalid pointer {}", right.offset, reg_text(left)));
            return null_reg;
        }
        switch (segment->type()) {
        case SegmentType::Locals:
        case SegmentType::Script:
        case SegmentType::Stack:
        case SegmentType::DynMem:
            return make_reg(left.segment, left.offset + right.to_sint16());
        default:
            return arithmetic_workaround(left, right, "addition");
        }
    }
    if (left.is_number() && right.is_number()) {
        return number(left.to_sint16() + right.to_sint16());
    }
    return arithmetic_workaround(left, right, "addition");
}

Reg Machine::subtract(Reg left, Reg right) {
    if (left.segment == right.segment) {
        return number(left.to_sint16() - right.to_sint16());
    }
    return add(left, make_reg(right.segment, -right.to_sint16()));
}

int Machine::compare(Reg left, Reg right, bool unsigned_compare) {
    if (left.segment == right.segment) {
        if (unsigned_compare || !left.is_number()) {
            return left.to_uint16() - right.to_uint16();
        }
        return left.to_sint16() - right.to_sint16();
    }
    // SCI0 scripts compare pointers with numbers up to 2000 to tell them
    // from resource numbers; a pointer is greater.
    const auto pointer_vs_integer = [](Reg pointer, Reg integer) {
        return pointer.is_pointer() && integer.is_number() && integer.offset <= 2000;
    };
    if (pointer_vs_integer(left, right)) {
        return 1;
    }
    if (pointer_vs_integer(right, left)) {
        return -1;
    }
    return arithmetic_workaround(left, right, "comparison").to_sint16();
}

std::uint16_t Machine::require_uint16(Reg value) {
    if (value.is_number()) {
        return value.to_uint16();
    }
    return arithmetic_workaround(value, null_reg, "require unsigned number").to_uint16();
}

std::int16_t Machine::require_sint16(Reg value) {
    if (value.is_number()) {
        return value.to_sint16();
    }
    return arithmetic_workaround(value, null_reg, "require signed number").to_sint16();
}

// Selectors --------------------------------------------------------------------

Machine::SelectorKind Machine::lookup_selector(Reg object, Selector selector, Reg* variable_object, int* variable_index,
                                               Reg* function) {
    const Object* obj = segments_.object(object);
    if (obj == nullptr) {
        faults_.raise(std::format("lookupSelector: Attempt to send to non-object or invalid script. Address {}",
                                  reg_text(object)));
        return SelectorKind::None;
    }
    const int index = obj->locate_var_selector(segments_, selector);
    if (index >= 0) {
        if (variable_object != nullptr) {
            *variable_object = object;
        }
        if (variable_index != nullptr) {
            *variable_index = index;
        }
        return SelectorKind::Variable;
    }
    while (obj != nullptr) {
        const int method = obj->func_selector_position(selector);
        if (method >= 0) {
            if (function != nullptr) {
                *function = obj->function(static_cast<std::uint16_t>(method));
            }
            return SelectorKind::Method;
        }
        obj = segments_.object(obj->super_class());
    }
    return SelectorKind::None;
}

Reg Machine::read_selector(Reg object, Selector selector) {
    Reg var_object;
    int var_index = 0;
    if (selector < 0 || lookup_selector(object, selector, &var_object, &var_index, nullptr) != SelectorKind::Variable) {
        return null_reg;
    }
    const Object* obj = segments_.object(var_object);
    return obj != nullptr ? obj->variable(static_cast<std::size_t>(var_index)) : null_reg;
}

void Machine::write_selector(Reg object, Selector selector, Reg value) {
    Reg var_object;
    int var_index = 0;
    if (selector < 0 || static_cast<std::size_t>(selector) > selectors_.size()) {
        faults_.raise(std::format("Attempt to write to invalid selector {}. Address {}", selector, reg_text(object)));
        return;
    }
    if (lookup_selector(object, selector, &var_object, &var_index, nullptr) != SelectorKind::Variable) {
        faults_.raise(std::format("Selector '{}' of object could not be written to. Address {}",
                                  selectors_.name(selector), reg_text(object)));
        return;
    }
    if (Object* obj = segments_.object(var_object)) {
        obj->variable_ref(static_cast<std::size_t>(var_index)) = value;
    }
}

void Machine::invoke_selector(Reg object, Selector selector, std::span<const Reg> arguments) {
    // The frame goes after the arguments of the kernel call that invokes
    // the selector.
    if (stack_frames_.empty() || stack_frames_.back().type != ExecType::Kernel) {
        faults_.raise("invokeSelector: not called from a kernel function");
        return;
    }
    const StackIndex frame = stack_frames_.back().sp;
    std::vector<Reg>& stack = stack_->entries;
    if (frame + 2 + arguments.size() > stack.size()) {
        faults_.raise("invokeSelector: stack overflow");
        return;
    }
    stack[frame] = number(selector);
    stack[frame + 1] = number(static_cast<int>(arguments.size()));
    const SelectorKind kind = lookup_selector(object, selector, nullptr, nullptr, nullptr);
    if (kind == SelectorKind::None) {
        faults_.raise(std::format("invokeSelector: Selector '{}' could not be invoked. Address {}",
                                  selectors_.name(selector), reg_text(object)));
        return;
    }
    if (kind == SelectorKind::Variable) {
        faults_.raise(std::format("invokeSelector: Attempting to invoke variable selector {}. Address {}",
                                  selectors_.name(selector), reg_text(object)));
        return;
    }
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        stack[frame + 2 + i] = arguments[i];
    }
    ExecStack* frame_stack = send_selector(object, object, frame, static_cast<int>(2 + arguments.size()), frame);
    if (frame_stack == nullptr) {
        return;
    }
    frame_stack->sp += arguments.size() + 2;
    frame_stack->fp += arguments.size() + 2;
    run_vm();
}

// Sends and calls ---------------------------------------------------------------

void Machine::exec_var_selectors() {
    while (!stack_frames_.empty() && stack_frames_.back().type == ExecType::VarSelector) {
        const ExecStack& frame = stack_frames_.back();
        Object* obj = segments_.object(frame.var_object);
        if (obj == nullptr) {
            faults_.raise("Invalid varselector exec stack entry");
            return;
        }
        Reg& var = obj->variable_ref(static_cast<std::size_t>(frame.var_index));
        if (frame.argc != 0) {
            var = stack_->entries[frame.argp + 1];
        } else {
            acc_ = var;
        }
        stack_frames_.pop_back();
    }
}

ExecStack* Machine::send_selector(Reg send_obj, Reg work_obj, StackIndex sp, int frame_size, StackIndex argp) {
    const int origin = static_cast<int>(stack_frames_.size()) - 1;
    auto insert_before = stack_frames_.end();
    const std::vector<Reg>& stack = stack_->entries;
    while (frame_size > 0) {
        const Selector selector = require_uint16(stack[argp]);
        ++argp;
        const int argc = require_uint16(stack[argp]);
        if (argc > 0x800) {
            faults_.raise("send_selector(): More than 0x800 arguments to function call");
            return nullptr;
        }
        Reg var_object;
        int var_index = 0;
        Reg function;
        const SelectorKind kind = lookup_selector(send_obj, selector, &var_object, &var_index, &function);
        if (kind == SelectorKind::None) {
            faults_.raise(std::format(
                "Send to invalid selector 0x{:x} ({}) of object at {}", static_cast<unsigned>(selector) & 0xFFFFU,
                selectors_.name(static_cast<Selector>(static_cast<unsigned>(selector) & 0xFFFFU)), reg_text(send_obj)));
            return nullptr;
        }
        ExecStack frame;
        frame.objp = work_obj;
        frame.sendp = send_obj;
        frame.argc = argc;
        frame.argp = argp;
        frame.debug_selector = selector;
        frame.debug_origin = origin;
        if (kind == SelectorKind::Method) {
            frame.type = ExecType::Call;
            frame.sp = sp;
            frame.fp = sp;
            frame.pc = function;
            frame.local_segment = function.segment;
            sp = carry;
        } else {
            frame.type = ExecType::VarSelector;
            frame.sp = carry;
            frame.fp = carry;
            frame.pc = null_reg;
            frame.local_segment = 0;
            frame.var_object = var_object;
            frame.var_index = var_index;
        }
        // Put the new entries on the stack in reverse order, so that the
        // first one runs first.
        insert_before = stack_frames_.insert(insert_before, frame);
        frame_size -= 2 + argc;
        argp += static_cast<StackIndex>(argc) + 1;
    }
    exec_var_selectors();
    return stack_frames_.empty() ? nullptr : &stack_frames_.back();
}

ExecStack* Machine::execute_method(int script_number, int export_index, StackIndex sp, Reg calling_obj, int argc,
                                   StackIndex argp) {
    SegmentId segment = segments_.script_segment(script_number);
    const Script* script = segments_.script_if_loaded(segment);
    if (script == nullptr || script->is_marked_deleted()) {
        segment = segments_.instantiate_script(script_number);
        script = segments_.script(segment);
        if (script == nullptr) {
            return nullptr;
        }
    }
    const auto address = script->export_offset(export_index);
    if (!address) {
        faults_.raise(
            std::format("Script {} validateExportFunc(): pubfunct {} is invalid", script_number, export_index));
        return nullptr;
    }
    if (*address == 0) {
        return nullptr;
    }
    ExecStack frame;
    frame.objp = calling_obj;
    frame.sendp = calling_obj;
    frame.sp = sp;
    frame.fp = sp;
    frame.argc = argc;
    frame.argp = argp;
    frame.pc = make_reg(segment, static_cast<int>(*address));
    frame.local_segment = segment;
    frame.debug_export = export_index;
    frame.debug_origin = static_cast<int>(stack_frames_.size()) - 1;
    frame.type = ExecType::Call;
    stack_frames_.push_back(frame);
    return &stack_frames_.back();
}

// Kernel calls --------------------------------------------------------------------

void Machine::call_kernel(int kernel_number, int argc) {
    if (!kernel_log_) {
        call_kernel_untraced(kernel_number, argc);
        return;
    }
    const StackIndex args = xs_->sp + 1;
    std::vector<Reg> arguments(stack_->entries.begin() + static_cast<std::ptrdiff_t>(args),
                               stack_->entries.begin() +
                                   static_cast<std::ptrdiff_t>(args + static_cast<StackIndex>(argc)));
    const char* name = "?";
    std::size_t first = 0;
    if (kernel_number >= 0 && static_cast<std::size_t>(kernel_number) < kernel_.size()) {
        const KernelFunction& function = kernel_.function(static_cast<std::size_t>(kernel_number));
        name = function.name;
        if (!function.subfunctions.empty() && argc >= 1 && !arguments[0].is_pointer() &&
            arguments[0].to_uint16() < function.subfunctions.size() &&
            function.subfunctions[arguments[0].to_uint16()].name != nullptr) {
            name = function.subfunctions[arguments[0].to_uint16()].name;
            first = 1;
        }
    }
    const Reg caller = xs_ != nullptr ? xs_->pc : null_reg;
    call_kernel_untraced(kernel_number, argc);
    kernel_log_(KernelCall{
        .name = name,
        .arguments = std::vector<Reg>(arguments.begin() + static_cast<std::ptrdiff_t>(first), arguments.end()),
        .result = acc_,
        .caller = caller,
    });
}

void Machine::call_kernel_untraced(int kernel_number, int argc) {
    if (kernel_number < 0 || static_cast<std::size_t>(kernel_number) >= kernel_.size()) {
        faults_.raise(std::format("Invalid kernel function 0x{:x} requested", kernel_number));
        return;
    }
    const KernelFunction& function = kernel_.function(static_cast<std::size_t>(kernel_number));
    StackIndex argv = xs_->sp + 1;
    std::vector<Reg>& stack = stack_->entries;
    if (argv + static_cast<StackIndex>(argc) > stack.size()) {
        faults_.raise("Kernel call arguments beyond the stack");
        return;
    }
    const auto args_span = [&](StackIndex start, int count) {
        return std::span<Reg>(stack).subspan(start, static_cast<std::size_t>(count));
    };
    const auto check = [&](const Signature& signature, std::span<const WorkaroundEntry> workarounds,
                           std::span<Reg> args, const char* name) -> bool {
        if (!signature || Kernel::signature_match(segments_, *signature, args)) {
            return true;
        }
        const WorkaroundSolution solution = find_workaround(0, workarounds);
        switch (solution.type) {
        case WorkaroundType::None:
            faults_.raise(std::format("[VM] k{}: signature mismatch", name));
            return false;
        case WorkaroundType::Ignore:
            return false;
        case WorkaroundType::StillCall:
            return true;
        case WorkaroundType::Fake:
            acc_ = number(solution.value);
            return false;
        }
        return false;
    };
    if (!check(function.signature, function.workarounds, args_span(argv, argc), function.name)) {
        return;
    }

    ExecStack kernel_frame;
    kernel_frame.argc = argc;
    kernel_frame.sp = argv + static_cast<StackIndex>(argc);
    kernel_frame.fp = kernel_frame.sp;
    kernel_frame.argp = argv - 1;
    kernel_frame.debug_kernel = kernel_number;
    kernel_frame.debug_origin = static_cast<int>(stack_frames_.size()) - 1;
    kernel_frame.type = ExecType::Kernel;

    if (function.subfunctions.empty()) {
        stack[argv - 1] = number(argc);
        stack_frames_.push_back(kernel_frame);
        acc_ = function.function(*this, args_span(argv, argc));
    } else {
        if (argc < 1) {
            faults_.raise(std::format("[VM] k{}: no subfunction ID parameter given", function.name));
            return;
        }
        if (stack[argv].is_pointer()) {
            faults_.raise(std::format("[VM] k{}: given subfunction ID is actually a pointer", function.name));
            return;
        }
        const std::uint16_t sub_id = stack[argv].to_uint16();
        --argc;
        ++argv;
        if (sub_id >= function.subfunctions.size() || function.subfunctions[sub_id].function == nullptr) {
            faults_.raise(
                std::format("[VM] k{}: subfunction ID {} requested, but not available", function.name, sub_id));
            return;
        }
        const KernelSubFunction& sub = function.subfunctions[sub_id];
        if (!check(sub.signature, sub.workarounds, args_span(argv, argc), sub.name)) {
            return;
        }
        stack[argv - 1] = number(argc);
        kernel_frame.argc = argc;
        kernel_frame.argp = argv - 1;
        kernel_frame.debug_kernel_sub = sub_id;
        stack_frames_.push_back(kernel_frame);
        acc_ = sub.function(*this, args_span(argv, argc));
    }
    // Remove the kernel frame again, if there is still an execution stack.
    if (!stack_frames_.empty()) {
        stack_frames_.pop_back();
    }
}

Reg Machine::kernel_argument(std::size_t index) const {
    if (stack_frames_.empty()) {
        return null_reg;
    }
    const StackIndex at = stack_frames_.back().argp + 1 + index;
    return at < stack_->entries.size() ? stack_->entries[at] : null_reg;
}

void Machine::shrink_stack_to_base() {
    if (!stack_frames_.empty()) {
        const int frames = execution_stack_base_ + 1;
        const auto size = static_cast<std::size_t>(frames);
        while (stack_frames_.size() > size) {
            stack_frames_.pop_back();
        }
    }
}

// Variables ------------------------------------------------------------------------

Reg Machine::read_var(int type, int index) {
    const Variables& vars = variables_[static_cast<std::size_t>(type)];
    const auto position = static_cast<std::ptrdiff_t>(vars.start) + index;
    bool valid = index >= 0 && index < vars.max;
    if (!valid && (type == var_param || type == var_temp)) {
        // Out of range, but within the stack: SSCI allowed it.
        valid = position >= 0 && std::cmp_less(position, stack_->entries.size());
    }
    if (!valid || vars.store == nullptr) {
        if (type != var_param && type != var_temp) {
            debug(std::format("[VM] Attempt to use invalid variable {} of type {}", index, type));
        }
        return acc_;
    }
    Reg& value = (*vars.store)[static_cast<std::size_t>(position)];
    if (value.segment == uninitialized_segment) {
        if (type == var_temp) {
            const WorkaroundSolution solution = find_workaround(index, uninitialized_read_workarounds());
            if (solution.type == WorkaroundType::None) {
                // As in ScummVM release builds: a warning and 0.
                warn(std::format("Uninitialized read for temp {}", index));
                value = null_reg;
            } else {
                value = number(solution.value);
            }
        } else if (type == var_param) {
            const WorkaroundSolution solution = find_workaround(index, uninitialized_read_for_param_workarounds());
            if (solution.type == WorkaroundType::None) {
                warn(std::format("Uninitialized read for parameter {}", index));
                return null_reg;
            }
            return number(solution.value);
        }
    }
    return value;
}

void Machine::write_var(int type, int index, Reg value) {
    const Variables& vars = variables_[static_cast<std::size_t>(type)];
    const auto position = static_cast<std::ptrdiff_t>(vars.start) + index;
    bool valid = index >= 0 && index < vars.max;
    if (!valid && (type == var_param || type == var_temp)) {
        valid = position >= 0 && std::cmp_less(position, stack_->entries.size());
    }
    if (!valid || vars.store == nullptr) {
        return;
    }
    // Writing an uninitialised value into a temp initialises it.
    if (type == var_temp && value.segment == uninitialized_segment) {
        value.segment = 0;
    }
    (*vars.store)[static_cast<std::size_t>(position)] = value;
}

Reg& Machine::property(Object* object, int index) {
    // Invalid property offsets read and write a dummy, as SSCI used
    // whatever memory followed the object.
    static Reg dummy;
    if (object == nullptr) {
        faults_.raise("validate_property: Sending to disposed object");
        dummy = null_reg;
        return dummy;
    }
    index = half_floor(index);
    if (index < 0 || static_cast<std::size_t>(index) >= object->var_count()) {
        dummy = null_reg;
        return dummy;
    }
    return object->variable_ref(static_cast<std::size_t>(index));
}

void Machine::push(Reg value) {
    if (xs_->sp >= stack_->entries.size()) {
        faults_.raise("[VM] Stack overflow");
        return;
    }
    stack_->entries[xs_->sp++] = value;
}

Reg Machine::pop() {
    if (xs_->sp == 0) {
        faults_.raise("[VM] Stack underflow");
        return null_reg;
    }
    return stack_->entries[--xs_->sp];
}

std::uint32_t Machine::find_offset(std::int16_t relative, std::uint32_t pc_offset) {
    // SCI0: relative to the program counter.
    return static_cast<std::uint16_t>(static_cast<std::int64_t>(pc_offset) + relative);
}

void Machine::setup_frame() {
    xs_ = &stack_frames_.back();
    stack_position_changed_ = false;
    const Script* local = segments_.script_if_loaded(xs_->local_segment);
    if (local == nullptr) {
        faults_.raise(std::format("Could not find local script from segment {:x}", xs_->local_segment));
        return;
    }
    variables_[var_local] = Variables{
        .store = local->locals_block() != nullptr ? &local->locals_block()->locals : nullptr,
        .start = 0,
        .segment = local->locals_segment(),
        .max = local->locals_count(),
    };
    const SegmentId stack_segment = segments_.find_segment_by_type(SegmentType::Stack);
    variables_[var_temp] =
        Variables{.store = &stack_->entries, .start = xs_->fp, .segment = stack_segment, .max = xs_->temp_count};
    variables_[var_param] =
        Variables{.store = &stack_->entries, .start = xs_->argp, .segment = stack_segment, .max = xs_->argc + 1};
}

// The interpreter ------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the bytecode dispatch switch, as in ScummVM.
void Machine::run_vm() {
    rest_ = 0;
    if (stack_frames_.empty()) {
        return;
    }
    const int old_base = execution_stack_base_;
    execution_stack_base_ = static_cast<int>(stack_frames_.size()) - 1;
    stack_position_changed_ = true;
    xs_ = &stack_frames_.back();
    if (segments_.script_if_loaded(xs_->local_segment) == nullptr) {
        faults_.raise("run_vm(): program counter gone astray (local_script pointer is null)");
        return;
    }
    Script* scr = nullptr;
    Object* obj = nullptr;
    const Script* local_script = nullptr;
    ExecStack* xs_new = nullptr;

    while (true) {
        if (abort_ != Abort::None || faults_.raised()) {
            return;
        }
        if (stack_position_changed_) {
            scr = segments_.script_if_loaded(stack_frames_.back().pc.segment);
            if (scr == nullptr) {
                faults_.raise(std::format("No script in segment {}", stack_frames_.back().pc.segment));
                return;
            }
            setup_frame();
            if (faults_.raised()) {
                return;
            }
            obj = segments_.object(xs_->objp);
            local_script = segments_.script_if_loaded(xs_->local_segment);
        }
        if (xs_->sp < xs_->fp) {
            faults_.raise("run_vm(): stack underflow");
            return;
        }
        if (scr == nullptr) { // Set with the first frame; kept for the analyzer.
            faults_.raise("run_vm(): no script");
            return;
        }
        if (xs_->pc.offset >= scr->size()) {
            faults_.raise(std::format("run_vm(): program counter gone astray, addr: {}, code buffer size: {}",
                                      xs_->pc.offset, scr->size()));
            return;
        }
        const Instruction ins = read_instruction(scr->buffer(), xs_->pc.offset);
        xs_->pc.offset = static_cast<std::uint16_t>(xs_->pc.offset + ins.length);
        const int opcode = ins.ext >> 1U;
        const auto& op = ins.params;

        switch (opcode) {
        case 0x00: // bnot
            acc_ = number(static_cast<int>(0xFFFFU ^ require_uint16(acc_)));
            break;
        case 0x01: { // add
            const Reg left = pop();
            acc_ = add(left, acc_);
            break;
        }
        case 0x02: { // sub
            const Reg left = pop();
            acc_ = subtract(left, acc_);
            break;
        }
        case 0x03: { // mul
            const Reg left = pop();
            acc_ = left.is_number() && acc_.is_number() ? number(left.to_sint16() * acc_.to_sint16())
                                                        : arithmetic_workaround(left, acc_, "multiplication");
            break;
        }
        case 0x04: { // div
            const Reg left = pop();
            acc_ = left.is_number() && acc_.is_number() && !acc_.is_null()
                       ? number(left.to_sint16() / acc_.to_sint16())
                       : arithmetic_workaround(left, acc_, "division");
            break;
        }
        case 0x05: { // mod
            const Reg left = pop();
            if (left.is_number() && acc_.is_number() && !acc_.is_null()) {
                if (left.to_sint16() < 0 || acc_.to_sint16() < 0) {
                    warn("Modulo of a negative number has been requested for SCI0. This *could* lead to issues");
                }
                const int value = left.to_sint16();
                const int modulo = std::abs(static_cast<int>(acc_.to_sint16()));
                int result = static_cast<std::int16_t>(value % modulo);
                if (result < 0) {
                    result += modulo;
                }
                acc_ = number(result);
            } else {
                acc_ = arithmetic_workaround(left, acc_, "modulo");
            }
            break;
        }
        case 0x06: { // shr
            const Reg left = pop();
            acc_ = left.is_number() && acc_.is_number() ? number(left.to_uint16() >> acc_.to_uint16())
                                                        : arithmetic_workaround(left, acc_, "shift right");
            break;
        }
        case 0x07: { // shl
            const Reg left = pop();
            acc_ = left.is_number() && acc_.is_number() ? number(left.to_uint16() << acc_.to_uint16())
                                                        : arithmetic_workaround(left, acc_, "shift left");
            break;
        }
        case 0x08: { // xor
            const Reg left = pop();
            acc_ = left.is_number() && acc_.is_number() ? number(left.to_uint16() ^ acc_.to_uint16())
                                                        : arithmetic_workaround(left, acc_, "bitwise XOR");
            break;
        }
        case 0x09: { // and
            const Reg left = pop();
            acc_ = left.is_number() && acc_.is_number() ? number(left.to_uint16() & acc_.to_uint16())
                                                        : arithmetic_workaround(left, acc_, "bitwise AND");
            break;
        }
        case 0x0a: { // or
            const Reg left = pop();
            acc_ = left.is_number() && acc_.is_number() ? number(left.to_uint16() | acc_.to_uint16())
                                                        : arithmetic_workaround(left, acc_, "bitwise OR");
            break;
        }
        case 0x0b: // neg
            acc_ = number(-require_sint16(acc_));
            break;
        case 0x0c: // not
            acc_ = number(acc_.is_null() ? 1 : 0);
            break;
        case 0x0d: { // eq?
            prev_ = acc_;
            const Reg left = pop();
            acc_ = number(left == acc_ ? 1 : 0);
            break;
        }
        case 0x0e: { // ne?
            prev_ = acc_;
            const Reg left = pop();
            acc_ = number(left != acc_ ? 1 : 0);
            break;
        }
        case 0x0f:   // gt?
        case 0x10:   // ge?
        case 0x11:   // lt?
        case 0x12:   // le?
        case 0x13:   // ugt?
        case 0x14:   // uge?
        case 0x15:   // ult?
        case 0x16: { // ule?
            prev_ = acc_;
            const Reg left = pop();
            const int result = compare(left, acc_, opcode >= 0x13);
            const int kind = (opcode - 0x0f) % 4;
            bool value = false;
            switch (kind) {
            case 0:
                value = result > 0;
                break;
            case 1:
                value = result >= 0;
                break;
            case 2:
                value = result < 0;
                break;
            default:
                value = result <= 0;
                break;
            }
            acc_ = number(value ? 1 : 0);
            break;
        }
        case 0x17: // bt
            if (!acc_.is_null()) {
                xs_->pc.offset = static_cast<std::uint16_t>(xs_->pc.offset + op[0]);
            }
            if (xs_->pc.offset >= local_script->size()) {
                faults_.raise(
                    std::format("[VM] op_bt: request to jump past the end of script {}", local_script->number()));
            }
            break;
        case 0x18: // bnt
            if (acc_.is_null()) {
                xs_->pc.offset = static_cast<std::uint16_t>(xs_->pc.offset + op[0]);
            }
            if (xs_->pc.offset >= local_script->size()) {
                faults_.raise(
                    std::format("[VM] op_bnt: request to jump past the end of script {}", local_script->number()));
            }
            break;
        case 0x19: // jmp
            xs_->pc.offset = static_cast<std::uint16_t>(xs_->pc.offset + op[0]);
            if (xs_->pc.offset >= local_script->size()) {
                faults_.raise(
                    std::format("[VM] op_jmp: request to jump past the end of script {}", local_script->number()));
            }
            break;
        case 0x1a: // ldi
            acc_ = number(op[0]);
            break;
        case 0x1b: // push
            push(acc_);
            break;
        case 0x1c: // pushi
            push(number(op[0]));
            break;
        case 0x1d: // toss
            --xs_->sp;
            break;
        case 0x1e: // dup
            push(stack_->entries[xs_->sp - 1]);
            break;
        case 0x1f: { // link
            xs_->temp_count = op[0];
            variables_[var_temp].max = op[0];
            // Temporaries start uninitialised, to catch reads before writes.
            for (int i = 0; i < op[0]; ++i) {
                stack_->entries[xs_->sp + static_cast<StackIndex>(i)] = make_reg(uninitialized_segment, 0);
            }
            xs_->sp += static_cast<StackIndex>(op[0]);
            break;
        }
        case 0x20: { // call
            const int argc = half_floor(op[1]) + 1 + rest_;
            const StackIndex call_base = xs_->sp - static_cast<StackIndex>(argc);
            const auto local_call = static_cast<std::uint16_t>(xs_->pc.offset + op[0]);
            const int final_argc = require_uint16(stack_->entries[call_base]) + rest_;
            stack_->entries[call_base] = number(final_argc);
            ExecStack frame;
            frame.objp = xs_->objp;
            frame.sendp = xs_->objp;
            frame.sp = xs_->sp;
            frame.fp = xs_->sp;
            frame.argc = final_argc;
            frame.argp = call_base;
            frame.local_segment = xs_->local_segment;
            frame.pc = make_reg(xs_->pc.segment, local_call);
            frame.debug_local_call = local_call;
            frame.debug_origin = static_cast<int>(stack_frames_.size()) - 1;
            frame.type = ExecType::Call;
            stack_frames_.push_back(frame);
            xs_new = &stack_frames_.back();
            rest_ = 0;
            xs_->sp = call_base;
            stack_position_changed_ = true;
            break;
        }
        case 0x21: { // callk
            if (gc_count_down_-- <= 0) {
                gc_count_down_ = gc_interval;
                run_gc();
            }
            xs_->sp -= static_cast<StackIndex>(half_floor(op[1]) + 1);
            xs_->sp -= static_cast<StackIndex>(rest_);
            const int argc = require_uint16(stack_->entries[xs_->sp]) + rest_;
            call_kernel(op[0], argc);
            rest_ = 0;
            // The kernel function may have run scripts itself.
            if (stack_frames_.empty()) {
                return;
            }
            xs_new = &stack_frames_.back();
            stack_position_changed_ = true;
            if (abort_ != Abort::None) {
                return;
            }
            break;
        }
        case 0x22:   // callb
        case 0x23: { // calle
            const bool external = opcode == 0x23;
            const int frame_args = half_floor(external ? op[2] : op[1]) + rest_ + 1;
            const StackIndex old_sp = xs_->sp;
            xs_->sp -= static_cast<StackIndex>(frame_args);
            stack_->entries[xs_->sp].offset = static_cast<std::uint16_t>(stack_->entries[xs_->sp].offset + rest_);
            xs_new = execute_method(external ? static_cast<std::uint16_t>(op[0]) : 0,
                                    external ? static_cast<std::uint16_t>(op[1]) : static_cast<std::uint16_t>(op[0]),
                                    old_sp, xs_->objp, stack_->entries[xs_->sp].offset, xs_->sp);
            rest_ = 0;
            if (xs_new != nullptr) {
                stack_position_changed_ = true;
            }
            break;
        }
        case 0x24: { // ret
            do {
                const StackIndex old_sp = xs_->sp;
                const ExecStack old = stack_frames_.back();
                if (static_cast<int>(stack_frames_.size()) - 1 == execution_stack_base_) {
                    execution_stack_base_ = old_base;
                    stack_frames_.pop_back();
                    stack_position_changed_ = true;
                    return;
                }
                if (old.type == ExecType::VarSelector) {
                    if (Object* var_obj = segments_.object(old.var_object)) {
                        Reg& var = var_obj->variable_ref(static_cast<std::size_t>(old.var_index));
                        if (old.argc != 0) {
                            var = stack_->entries[old.argp + 1];
                        } else {
                            acc_ = var;
                        }
                    }
                }
                stack_frames_.pop_back();
                stack_position_changed_ = true;
                xs_ = &stack_frames_.back();
                if (xs_->sp == carry || xs_->type != ExecType::Call) {
                    xs_->sp = old_sp;
                    xs_->fp = old_sp;
                }
            } while (xs_->type == ExecType::VarSelector);
            stack_position_changed_ = true;
            xs_new = xs_;
            break;
        }
        case 0x25:   // send
        case 0x2a: { // self
            const StackIndex old_sp = xs_->sp;
            xs_->sp -= static_cast<StackIndex>(half_floor(op[0]) + rest_);
            stack_->entries[xs_->sp + 1].offset =
                static_cast<std::uint16_t>(stack_->entries[xs_->sp + 1].offset + rest_);
            const Reg target = opcode == 0x25 ? acc_ : xs_->objp;
            xs_new =
                send_selector(target, target, old_sp, half_floor(op[0]) + static_cast<std::uint16_t>(rest_), xs_->sp);
            if (xs_new != nullptr && xs_new != xs_) {
                stack_position_changed_ = true;
            }
            rest_ = 0;
            break;
        }
        case 0x28: // class
            acc_ = segments_.class_address(static_cast<std::uint16_t>(op[0]), ScriptLoad::Lock, xs_->pc.segment);
            break;
        case 0x2b: { // super
            const Reg klass =
                segments_.class_address(static_cast<std::uint16_t>(op[0]), ScriptLoad::Load, xs_->pc.segment);
            if (!klass.is_pointer()) {
                faults_.raise("[VM]: Invalid superclass in object");
                break;
            }
            const StackIndex old_sp = xs_->sp;
            xs_->sp -= static_cast<StackIndex>(half_floor(op[1]) + rest_);
            stack_->entries[xs_->sp + 1].offset =
                static_cast<std::uint16_t>(stack_->entries[xs_->sp + 1].offset + rest_);
            xs_new =
                send_selector(klass, xs_->objp, old_sp, half_floor(op[1]) + static_cast<std::uint16_t>(rest_), xs_->sp);
            if (xs_new != nullptr && xs_new != xs_) {
                stack_position_changed_ = true;
            }
            rest_ = 0;
            break;
        }
        case 0x2c: { // rest
            int first = static_cast<std::uint16_t>(op[0]);
            rest_ = std::max(xs_->argc - first + 1, 0);
            for (; first <= xs_->argc; ++first) {
                push(stack_->entries[xs_->argp + static_cast<StackIndex>(first)]);
            }
            break;
        }
        case 0x2d: { // lea
            const unsigned kind = static_cast<unsigned>(static_cast<std::uint16_t>(op[0])) >> 1U;
            const auto var_type = static_cast<int>(kind & 0x03U);
            const Variables& vars = variables_[static_cast<std::size_t>(var_type)];
            int offset = static_cast<int>(vars.start);
            if ((kind & 0x08U) != 0) {
                offset += require_sint16(acc_);
            }
            offset += op[1];
            acc_ = make_reg(vars.segment, offset * 2);
            break;
        }
        case 0x2e: // selfID
            acc_ = xs_->objp;
            break;
        case 0x30: // pprev
            push(prev_);
            break;
        case 0x31: // pToa
            acc_ = property(obj, op[0]);
            break;
        case 0x32: // aTop
            property(obj, op[0]) = acc_;
            break;
        case 0x33: // pTos
            push(property(obj, op[0]));
            break;
        case 0x34: { // sTop
            const Reg value = pop();
            property(obj, op[0]) = value;
            break;
        }
        case 0x35:   // ipToa
        case 0x36:   // dpToa
        case 0x37:   // ipTos
        case 0x38: { // dpTos
            Reg& prop = property(obj, op[0]);
            prop = (static_cast<unsigned>(opcode) & 1U) != 0 ? add(prop, number(1)) : subtract(prop, number(1));
            if (opcode == 0x35 || opcode == 0x36) {
                acc_ = prop;
            } else {
                push(prop);
            }
            break;
        }
        case 0x39:   // lofsa
        case 0x3a: { // lofss
            const Reg value = make_reg(xs_->pc.segment, static_cast<int>(find_offset(op[0], xs_->pc.offset)));
            if (value.offset >= scr->size()) {
                faults_.raise(
                    std::format("VM: lofsa/lofss operation overflowed: {} beyond end of script", reg_text(value)));
                break;
            }
            if (opcode == 0x39) {
                acc_ = value;
            } else {
                push(value);
            }
            break;
        }
        case 0x3b: // push0
            push(number(0));
            break;
        case 0x3c: // push1
            push(number(1));
            break;
        case 0x3d: // push2
            push(number(2));
            break;
        case 0x3e: // pushSelf (or the debug opcode file)
            if ((ins.ext & 1U) == 0) {
                push(xs_->objp);
            }
            break;
        case 0x3f: // line
            break;
        default:
            if (opcode >= 0x40) {
                const auto var_type = static_cast<int>(static_cast<unsigned>(opcode) & 0x03U);
                const bool indexed = (static_cast<unsigned>(opcode) & 0x08U) != 0;
                const int group = (opcode - 0x40) / 16; // 0 load, 1 store, 2 increment, 3 decrement
                const bool to_stack = (static_cast<unsigned>(opcode) & 0x04U) != 0;
                const int index = op[0] + (indexed ? require_sint16(acc_) : 0);
                switch (group) {
                case 0: // l[as][gltp][i]
                    if (to_stack) {
                        push(read_var(var_type, index));
                    } else {
                        acc_ = read_var(var_type, index);
                    }
                    break;
                case 1: // s[as][gltp][i]
                    if (to_stack) {
                        write_var(var_type, index, pop());
                    } else {
                        if (indexed) {
                            acc_ = pop();
                        }
                        write_var(var_type, index, acc_);
                    }
                    break;
                default: { // plus/minus
                    const Reg value = group == 2 ? add(read_var(var_type, index), number(1))
                                                 : subtract(read_var(var_type, index), number(1));
                    if (to_stack) {
                        push(value);
                    } else {
                        acc_ = value;
                    }
                    write_var(var_type, index, value);
                    break;
                }
                }
                break;
            }
            faults_.raise(std::format("run_vm(): illegal opcode {:x}", opcode));
            return;
        }

        if (stack_position_changed_) {
            xs_ = xs_new;
        }
        if (stack_frames_.empty() || xs_ != &stack_frames_.back()) {
            faults_.raise(std::format("xs is stale; last command was {:02x}", opcode));
            return;
        }
        ++steps_;
    }
}

} // namespace sci::vm
