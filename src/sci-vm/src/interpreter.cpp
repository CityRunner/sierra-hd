// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vm/interpreter.hpp"

#include "events.hpp"
#include "machine.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <format>
#include <iterator>

namespace sci::vm {

namespace {

void append_value(std::string& line, Reg value) {
    if (value.segment == 0) {
        std::format_to(std::back_inserter(line), "{}", value.offset);
    } else {
        std::format_to(std::back_inserter(line), "{:x}:{:04x}", value.segment, value.offset);
    }
}

} // namespace

RunResult run_deterministic(const resource::ResourceManager& resources, const RunOptions& options) {
    Subsystems subsystems;
    subsystems.set_seed(options.seed);
    for (const InputEvent& event : options.input) {
        subsystems.events().schedule(event);
    }
    Machine machine(resources, subsystems);
    Clock& clock = subsystems.clock();
    std::uint32_t calls = 0;
    const auto check_stop = [&] {
        if (clock.ticks() >= options.stop_tick || calls >= options.stop_calls) {
            machine.abort(Abort::QuitGame);
        }
    };
    clock.set_advance_hook(check_stop);
    machine.set_kernel_log([&](const KernelCall& call) {
        if (options.trace) {
            std::string line = std::format("{} {} {}(", calls, clock.ticks(), call.name);
            for (std::size_t i = 0; i < call.arguments.size(); ++i) {
                if (i != 0) {
                    line += ',';
                }
                append_value(line, call.arguments[i]);
            }
            line += ") = ";
            append_value(line, call.result);
            if (options.trace_callers) {
                const Script* script = machine.segments().script_if_loaded(call.caller.segment);
                std::format_to(std::back_inserter(line), " @{}:{:04x}", script != nullptr ? script->number() : -1,
                               call.caller.offset);
            }
            options.trace(line);
        }
        ++calls;
        check_stop();
    });
    if (!machine.faults().raised()) {
        subsystems.attach(machine);
        if (machine.init_game()) {
            machine.run_game();
        }
    }
    RunResult result{.fault = std::nullopt, .ticks = clock.ticks(), .calls = calls};
    if (machine.faults().raised()) {
        result.fault = machine.faults().message();
    }
    return result;
}

} // namespace sci::vm
