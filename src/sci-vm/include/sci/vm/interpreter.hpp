// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the game in the deterministic mode of milestone 4: virtual time, a
// seeded random source, input from a replay, and every kernel call traced
// as the reference ScummVM traces it (docs/formats/replay-and-trace.md).
// Milestone 5 adds the cast of every kAnimate call and the 1x priority and
// control maps, compared with the reference's as well; milestone 6a what
// the parser made of each typed command and each Said spec; milestone 6b
// the layout of windows, text, controls, menus and the cursor.

#pragma once

#include "sci/resource/resource_manager.hpp"
#include "sci/vm/events.hpp"
#include "sci/vm/files.hpp"
#include "sci/vm/reg.hpp"
#include "sci/vm/state_hash.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sci::audio {
class Mt32;
}

namespace sci::vm {

// One cast entry of a kAnimate call: the values GfxAnimate drew it with
// (loop and cel corrected, priority from its y unless fixed, the signal as
// written back at the end of the call).
struct CastEntry {
    Reg object;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
    std::int16_t priority = 0;
    int view = 0;
    std::int16_t loop = 0;
    std::int16_t cel = 0;
    std::uint16_t signal = 0;
};

// The 1x maps at a tick, each 320 x 200: the screen the player sees (EGA
// colours) and the priority and control maps the logic reads.
struct ScreenMaps {
    std::uint32_t tick = 0;
    std::span<const std::uint8_t> shown;
    std::span<const std::uint8_t> priority;
    std::span<const std::uint8_t> control;
};

struct SaveCheck {
    std::size_t bytes = 0;            // The save file's size.
    StateHash saved;                  // The state written.
    StateHash restored;               // The fresh machine's state after reading it.
    std::optional<std::string> error; // Why reading failed, if it did.
};

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
    // The files the game may open; none can be opened without a store.
    FileStore* files = nullptr;
    // The saved games kSaveGame writes and kRestoreGame reads; none are
    // listed and saving fails without a store.
    SaveStore* saves = nullptr;
    // Called whenever virtual time passes, with the game tick and the maps,
    // once graphics are up.
    std::function<void(const ScreenMaps&)> on_tick;
    // Called after the trace line of every kAnimate call, with the number
    // and tick of that line and the cast in drawing order (empty when the
    // call drew nothing).
    std::function<void(std::uint32_t call, std::uint32_t tick, std::span<const CastEntry>)> on_animate;
    // Called after the trace line of every kParse and kSaid call, with the
    // number and tick of that line and what the parser did, as a line of
    // the parser file after its tick and call number (milestone 6a,
    // docs/formats/replay-and-trace.md): `parse "<input>" ...` or
    // `said <spec> ...`, without the line break.
    std::function<void(std::uint32_t call, std::uint32_t tick, std::string_view)> on_parser;
    // Called for every window, text, control, menu and cursor operation as
    // it happens, with the number of the kernel call running, the tick and
    // a line of the layout file after its tick and call number (milestone
    // 6b, docs/formats/replay-and-trace.md), without the line break.
    std::function<void(std::uint32_t call, std::uint32_t tick, std::string_view)> on_layout;
    // When set, the music plays on the AdLib driver with the game's patches
    // (sci::audio::AdlibDriver; without it on the silent driver, which
    // changes nothing else), and this is called with every register write
    // the driver makes and the number of music timer calls made so far
    // (milestone 7, docs/formats/replay-and-trace.md, "OPL files"). The
    // chip itself generates no samples.
    std::function<void(std::uint32_t timer_call, std::uint16_t reg, std::uint8_t value)> on_opl;
    // When set, the music plays on the MT-32 driver with the game's
    // patch.001 (sci::audio::Mt32Driver) instead, as ScummVM's
    // MidiPlayer_Midi on an emulated MT-32: kDoSound GetPolyphony returns
    // 32 and the songs' MT-32 channels play. It gives the synthesiser the
    // driver feeds (nothing renders it), or null to only log the messages.
    // Not together with on_opl.
    std::function<std::unique_ptr<audio::Mt32>()> mt32;
    // With mt32: called with every message the driver sends (as
    // sci::audio::Mt32Driver::Log::on_send sees it) and the number of music
    // timer calls made so far (milestone 7, docs/formats/replay-and-trace.md,
    // "MIDI files").
    std::function<void(std::uint32_t timer_call, std::span<const std::uint8_t> message)> on_midi;
    // The hash of the VM state (milestone 8, docs/ASSUMPTIONS.md, "Milestone
    // 8: the VM state") at each of these ticks, in increasing order: when
    // virtual time first reaches the tick, on_state_hash is called with it,
    // the tick reached (a wait may pass several) and the hash. The state is
    // taken in the kernel call that let the time pass.
    std::vector<std::uint32_t> state_hash_ticks;
    std::function<void(std::uint32_t requested, std::uint32_t tick, const StateHash&)> on_state_hash;
    // The save round trip (milestone 8, docs/formats/save-format.md) at each
    // of these ticks, taken as state_hash_ticks are: the state is written as
    // a save file and read into a fresh machine on the same game files, and
    // on_save_check is called with what came of it. The run goes on with
    // its own machine, unchanged.
    std::vector<std::uint32_t> save_check_ticks;
    std::function<void(std::uint32_t requested, std::uint32_t tick, const SaveCheck&)> on_save_check;
};

struct RunResult {
    // Why the interpreter stopped early, if it did.
    std::optional<std::string> fault;
    std::uint32_t ticks = 0;
    std::uint32_t calls = 0;
};

[[nodiscard]] RunResult run_deterministic(const resource::ResourceManager& resources, const RunOptions& options);

} // namespace sci::vm
