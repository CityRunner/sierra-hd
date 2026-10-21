// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the game in the deterministic mode of milestone 4: virtual time, a
// seeded random source, input from a replay, and every kernel call traced
// as the reference ScummVM traces it (docs/formats/replay-and-trace.md).

#pragma once

#include "sci/resource/resource_manager.hpp"
#include "sci/vm/events.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sci::vm {

struct RunOptions {
    std::uint32_t seed = 0;
    std::vector<InputEvent> input;
    // The run ends when the virtual tick or the number of kernel calls
    // reaches these.
    std::uint32_t stop_tick = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t stop_calls = std::numeric_limits<std::uint32_t>::max();
    // Receives one trace line per kernel call, without the line break.
    std::function<void(std::string_view)> trace;
    // Appends " @<script>:<offset>" of the calling instruction to each line
    // (for debugging; the reference does not write it).
    bool trace_callers = false;
};

struct RunResult {
    // Why the interpreter stopped early, if it did.
    std::optional<std::string> fault;
    std::uint32_t ticks = 0;
    std::uint32_t calls = 0;
};

[[nodiscard]] RunResult run_deterministic(const resource::ResourceManager& resources, const RunOptions& options);

} // namespace sci::vm
