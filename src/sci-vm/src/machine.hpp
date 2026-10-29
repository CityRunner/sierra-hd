// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/vm.h, engines/sci/engine/vm.cpp,
//   engines/sci/engine/state.h, engines/sci/engine/state.cpp,
//   engines/sci/engine/vm_types.cpp, engines/sci/engine/selector.cpp,
//   engines/sci/sci.cpp (initGame, runGame) (SCI0 late paths)
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
#include "kernel.hpp"
#include "resources.hpp"
#include "script_patcher.hpp"
#include "seg_manager.hpp"
#include "selectors.hpp"
#include "workarounds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace sci::vm {

class StateWriter;
class StateReader;
class Subsystems;
enum class Execution : std::uint8_t;

inline constexpr int vm_stack_size = 0x1000;
inline constexpr int gc_interval = 0x8000;

enum class ExecType : std::uint8_t { Call = 0, Kernel = 1, VarSelector = 2 };

inline constexpr int var_global = 0;
inline constexpr int var_local = 1;
inline constexpr int var_temp = 2;
inline constexpr int var_param = 3;

// The global variables with a fixed meaning (engines/sci/engine/vm.h).
inline constexpr std::uint16_t global_ego = 0;
inline constexpr std::uint16_t global_game = 1;
inline constexpr std::uint16_t global_current_room = 2;
inline constexpr std::uint16_t global_speed = 3;
inline constexpr std::uint16_t global_quit = 4;
inline constexpr std::uint16_t global_cast = 5;
inline constexpr std::uint16_t global_sounds = 8;
inline constexpr std::uint16_t global_current_room_no = 11;
inline constexpr std::uint16_t global_previous_room_no = 12;
inline constexpr std::uint16_t global_new_room_no = 13;
inline constexpr std::uint16_t global_score = 15;
inline constexpr std::uint16_t global_fast_cast = 84;

// A position on the VM stack, as an index; `carry` marks the stack pointer
// that a send carries over to the method it calls (CALL_SP_CARRY).
using StackIndex = std::size_t;
inline constexpr StackIndex carry = static_cast<StackIndex>(-1);

struct ExecStack {
    Reg objp;       // The object executing.
    Reg sendp;      // The object that received the send.
    Reg pc;         // Program counter (not current for the top element).
    Reg var_object; // For VarSelector entries: the object and its property.
    int var_index = 0;
    StackIndex fp = 0;
    StackIndex sp = 0;
    int argc = 0;
    StackIndex argp = 0; // The arguments; argp[0] is argc.
    int temp_count = 0;
    SegmentId local_segment = 0;
    Selector debug_selector = no_selector;
    int debug_export = -1;
    int debug_local_call = -1;
    int debug_origin = -1;
    int debug_kernel = -1;
    int debug_kernel_sub = -1;
    ExecType type = ExecType::Call;
};

enum class Abort : std::uint8_t { None, LoadGame, RestartGame, QuitGame };

// What kGameIsRestarting reports (GAMEISRESTARTING_*): 1 after a restart,
// 2 after a restore.
enum class Restarting : std::uint8_t { None = 0, Restart = 1, Restore = 2 };

// kSaveGame's virtual ids of saved games (SAVEGAMEID_OFFICIALRANGE_*): slot
// n is id 100 + n.
inline constexpr int save_id_first = 100;
inline constexpr int save_id_last = 199;

// One kernel call as traced (docs/formats/replay-and-trace.md).
struct KernelCall {
    const char* name = "";
    std::vector<Reg> arguments;
    Reg result;
    Reg caller; // The instruction after the callk.
};

class Machine {
public:
    Machine(const resource::ResourceManager& resources, Subsystems& subsystems);
    ~Machine();
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;

    // Loads script 0 and the game object (initGame). False on a fault.
    bool init_game();
    // Runs the game's play method until it returns or a fault stops it
    // (runGame), restarting when the game asks for it.
    void run_game();
    // Goes on as run_game from a state read with Execution::Resume, which
    // was written at a resume point.
    void resume_game();

    // A resume point is the end of a kernel call made by the top-level
    // run_vm (not from a kernel function's scripts), with no abort pending:
    // there the interpreter keeps nothing outside the state, so a machine
    // that reads the state with Execution::Resume can go on from it. The
    // hook is called at each; when it returns true the machine stops there
    // (run_game and resume_game return, suspended() is true) and is not to
    // run again. For the round trip of sci-vm-trace.
    void set_suspend_hook(std::function<bool()> hook) { suspend_hook_ = std::move(hook); }

    [[nodiscard]] bool suspended() const { return suspended_; }

    // State used by the kernel functions.
    [[nodiscard]] SegManager& segments() { return segments_; }

    [[nodiscard]] Resources& resources() { return resources_; }

    [[nodiscard]] Faults& faults() { return faults_; }

    [[nodiscard]] Kernel& kernel() { return kernel_; }

    [[nodiscard]] const SelectorTable& selectors() const { return selectors_; }

    [[nodiscard]] const SelectorCache& selector() const { return selectors_.cache(); }

    [[nodiscard]] Subsystems& subsystems() { return *subsystems_; }

    [[nodiscard]] Reg game_object() const { return game_object_; }

    [[nodiscard]] Reg global(std::size_t index) const;
    void set_global(std::size_t index, Reg value);
    [[nodiscard]] std::uint16_t current_room_number() const;

    [[nodiscard]] Reg acc() const { return acc_; }

    void set_acc(Reg value) { acc_ = value; }

    void abort(Abort abort) { abort_ = abort; }

    [[nodiscard]] Abort aborting() const { return abort_; }

    [[nodiscard]] Restarting game_is_restarting() const { return game_is_restarting_; }

    void set_game_is_restarting(Restarting value) { game_is_restarting_ = value; }

    // kSaveGame's last new slot (EngineState::_lastSaveVirtualId and
    // _lastSaveNewId): a save with the same low virtual id overwrites it.
    [[nodiscard]] int last_save_virtual_id() const { return last_save_virtual_id_; }

    [[nodiscard]] int last_save_new_id() const { return last_save_new_id_; }

    void set_last_save(int virtual_id, int new_id) {
        last_save_virtual_id_ = virtual_id;
        last_save_new_id_ = new_id;
    }

    // gamestate_restore after the state is read: the GC runs a full
    // interval later, the scripts are told of the restore, and the last
    // save is forgotten (EngineState::reset).
    void after_restore();

    [[nodiscard]] std::uint32_t steps() const { return steps_; }

    // Selector access (engines/sci/engine/selector.cpp).
    [[nodiscard]] Reg read_selector(Reg object, Selector selector);

    [[nodiscard]] std::int16_t read_selector_value(Reg object, Selector selector) {
        return read_selector(object, selector).to_sint16();
    }

    void write_selector(Reg object, Selector selector, Reg value);

    void write_selector_value(Reg object, Selector selector, int value) {
        write_selector(object, selector, number(value));
    }

    // Calls a method from a kernel function and runs it to its end.
    void invoke_selector(Reg object, Selector selector, std::span<const Reg> arguments);
    enum class SelectorKind : std::uint8_t { None, Variable, Method };
    SelectorKind lookup_selector(Reg object, Selector selector, Reg* variable_object, int* variable_index,
                                 Reg* function);

    // Arithmetic on values (engines/sci/engine/vm_types.cpp).
    Reg add(Reg left, Reg right);
    Reg subtract(Reg left, Reg right);
    int compare(Reg left, Reg right, bool unsigned_compare);
    std::uint16_t require_uint16(Reg value);
    std::int16_t require_sint16(Reg value);

    // The kernel-call log: every call with its arguments and result.
    void set_kernel_log(std::function<void(const KernelCall&)> log) { kernel_log_ = std::move(log); }

    // The kernel calls made so far, counting the one in progress.
    [[nodiscard]] std::uint64_t kernel_calls() const { return kernel_calls_; }

    // Where scripts may write a workaround solution (trackOriginAndFindWorkaround).
    WorkaroundSolution find_workaround(int index, std::span<const WorkaroundEntry> list);

    [[nodiscard]] const std::list<ExecStack>& execution_stack() const { return stack_frames_; }

    [[nodiscard]] Reg stack_value(StackIndex index) const { return stack_->entries[index]; }

    [[nodiscard]] Reg r_prev() const { return prev_; }

    // An argument of the running kernel call, also past its argument count
    // (kGraph FillBoxAny reads the stack beyond them, as Sierra did).
    [[nodiscard]] Reg kernel_argument(std::size_t index) const;

    // kFlushResources runs the garbage collector.
    void collect_garbage() { run_gc(); }

    // EngineState::shrinkStackToBase.
    void shrink_stack_to_base();

    // Where the running run_vm started: 0 unless a kernel function invoked it.
    [[nodiscard]] int execution_stack_base() const { return execution_stack_base_; }

    // For tests that call kernel functions as the top-level run_vm would.
    void set_execution_stack_base(int base) { execution_stack_base_ = base; }

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;
    // Reads it back (state_load.cpp).
    void load_state(StateReader& in, Execution execution);

private:
    struct Variables {
        std::vector<Reg>* store = nullptr;
        StackIndex start = 0;  // Index of variable 0 in store.
        SegmentId segment = 0; // For lea.
        int max = 0;
    };

    void init_stack_base_with_selector(Selector selector);
    ExecStack* send_selector(Reg send_obj, Reg work_obj, StackIndex sp, int frame_size, StackIndex argp);
    void exec_var_selectors();
    ExecStack* execute_method(int script, int export_index, StackIndex sp, Reg calling_obj, int argc, StackIndex argp);
    void call_kernel(int number, int argc);
    void call_kernel_untraced(int number, int argc);
    // `resume`: the top-level run_vm going on from a resume point.
    void run_vm(bool resume = false);
    // run_game's loop, from the first run_vm.
    void game_loop(bool resume);
    void setup_frame();
    Reg read_var(int type, int index);
    void write_var(int type, int index, Reg value);
    Reg& property(Object* object, int index);
    void push(Reg value);
    Reg pop();
    Reg arithmetic_workaround(Reg left, Reg right, const char* operation);
    void run_gc();
    [[nodiscard]] static std::uint32_t find_offset(std::int16_t relative, std::uint32_t pc_offset);

    Resources resources_;
    Faults faults_;
    ScriptPatcher patcher_;
    SegManager segments_;
    SelectorTable selectors_;
    Kernel kernel_;
    Subsystems* subsystems_;

    Reg game_object_{};
    DataStack* stack_ = nullptr;
    std::list<ExecStack> stack_frames_;
    ExecStack* xs_ = nullptr;
    int execution_stack_base_ = -1;
    bool stack_position_changed_ = false;
    std::array<Variables, 4> variables_{};
    Reg acc_{};
    Reg prev_{};
    int rest_ = 0;
    Abort abort_ = Abort::None;
    Restarting game_is_restarting_ = Restarting::None;
    int last_save_virtual_id_ = save_id_first;
    int last_save_new_id_ = 0;
    int gc_count_down_ = 0;
    std::uint32_t steps_ = 0;
    std::function<void(const KernelCall&)> kernel_log_;
    std::uint64_t kernel_calls_ = 0;
    std::function<bool()> suspend_hook_;
    bool suspended_ = false;
};

} // namespace sci::vm
