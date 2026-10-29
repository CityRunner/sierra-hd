// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vm/interpreter.hpp"

#include "events.hpp"
#include "files.hpp"
#include "gfx_animate.hpp"
#include "gfx_screen.hpp"
#include "graphics.hpp"
#include "machine.hpp"
#include "music.hpp"
#include "parser.hpp"
#include "save_game.hpp"
#include "sci/audio/adlib.hpp"
#include "sci/audio/mt32.hpp"
#include "sci/audio/mt32_driver.hpp"
#include "sound.hpp"
#include "state.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sci::vm {

namespace {

void append_value(std::string& line, Reg value) {
    if (value.segment == 0) {
        std::format_to(std::back_inserter(line), "{}", value.offset);
    } else {
        std::format_to(std::back_inserter(line), "{:x}:{:04x}", value.segment, value.offset);
    }
}

// One trace line, `<call> <tick> <name>(<arguments>) = <result>`, with the
// caller's script and offset when `callers` is given.
std::string trace_line(const KernelCall& call, std::uint32_t number, std::uint32_t tick, const SegManager* callers) {
    std::string line = std::format("{} {} {}(", number, tick, call.name);
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        if (i != 0) {
            line += ',';
        }
        append_value(line, call.arguments[i]);
    }
    line += ") = ";
    append_value(line, call.result);
    if (callers != nullptr) {
        const Script* script = callers->script_if_loaded(call.caller.segment);
        std::format_to(std::back_inserter(line), " @{}:{:04x}", script != nullptr ? script->number() : -1,
                       call.caller.offset);
    }
    return line;
}

// The parser line of a kParse or kSaid call, after its trace line (when the
// parser records); kParse's with the event's type and claimed flag as the
// call leaves them.
void report_parser(Machine& machine, Parser& parser, const KernelCall& call, std::uint32_t number, std::uint32_t tick,
                   const RunOptions& options) {
    ParserRecord* const recorded = parser.record();
    if (recorded == nullptr) {
        return;
    }
    ParserRecord& record = *recorded;
    const std::string_view name = call.name;
    if (name == "Parse" && !record.parse.empty()) {
        const SelectorCache& selectors = machine.selector();
        const Reg event = record.parse_event;
        options.on_parser(number, tick,
                          std::format("{} type={} claimed={}", record.parse,
                                      machine.read_selector(event, selectors.type).to_uint16(),
                                      machine.read_selector(event, selectors.claimed).to_uint16()));
        record.parse.clear();
    } else if (name == "Said" && !record.said.empty()) {
        options.on_parser(number, tick, record.said);
        record.said.clear();
    }
}

// The cast the kAnimate call just drew, in drawing order.
void report_cast(const Animate& animate, std::vector<CastEntry>& cast) {
    cast.clear();
    for (const AnimateEntry& entry : animate.drawn_cast()) {
        cast.push_back(CastEntry{
            .object = entry.object,
            .x = entry.x,
            .y = entry.y,
            .z = entry.z,
            .priority = entry.priority,
            .view = entry.view,
            .loop = entry.loop,
            .cel = entry.cel,
            .signal = entry.signal,
        });
    }
}

// The music device of RunOptions::on_opl (the AdLib driver and its
// register log) or RunOptions::mt32 (the MT-32 driver and its MIDI log).
// It is opened before the player, as ScummVM's SciMusic::init does, so its
// start-up messages come before the first timer call, and it is detached
// from the player before it goes (the player's parsers send note-offs when
// they are destroyed).
class MusicLog {
public:
    MusicLog(const RunOptions& options, Sound& sound) : options_(&options), sound_(&sound) {}

    ~MusicLog() {
        if (adlib_ || mt32_) {
            sound_->music().set_device(nullptr);
        }
    }

    MusicLog(const MusicLog&) = delete;
    MusicLog& operator=(const MusicLog&) = delete;
    MusicLog(MusicLog&&) = delete;
    MusicLog& operator=(MusicLog&&) = delete;

    // Without on_opl or mt32, nothing. Otherwise the driver with the game's
    // patches becomes the player's device; the error if the patches cannot
    // be read.
    std::optional<std::string> open(const resource::ResourceManager& resources) {
        if (options_->mt32) {
            if (options_->on_opl) {
                return "the OPL log needs the AdLib driver";
            }
            auto patch = audio::load_mt32_patch(resources);
            if (!patch) {
                return patch.error().message;
            }
            if (options_->on_midi) {
                log_.on_send = [this](std::span<const std::uint8_t> message) {
                    options_->on_midi(music_ != nullptr ? music_->timer_calls() : 0, message);
                };
            }
            mt32_ = std::make_unique<audio::Mt32Driver>(options_->mt32(), *patch, options_->on_midi ? &log_ : nullptr);
            sound_->set_device(mt32_.get());
            return std::nullopt;
        }
        if (!options_->on_opl) {
            return std::nullopt;
        }
        auto patches = audio::load_adlib_patches(resources);
        if (!patches) {
            return patches.error().message;
        }
        adlib_ =
            std::make_unique<audio::AdlibDriver>(std::move(*patches), [this](std::uint16_t reg, std::uint8_t value) {
                options_->on_opl(music_ != nullptr ? music_->timer_calls() : 0, reg, value);
            });
        sound_->set_device(adlib_.get());
        return std::nullopt;
    }

    // The player is attached: its timer calls key the log from now on.
    void attached() {
        if (adlib_ || mt32_) {
            music_ = &sound_->music();
        }
    }

    // The device goes over to another player, attached and restored (a
    // round trip): the same device plays on.
    void move_to(Sound& sound) {
        audio::MidiDevice* const device = adlib_ ? static_cast<audio::MidiDevice*>(adlib_.get()) : mt32_.get();
        if (device == nullptr) {
            return;
        }
        sound_->music().set_device(nullptr);
        sound_->set_device(nullptr);
        sound_ = &sound;
        sound.set_device(device);
        sound.music().set_device(device);
        music_ = &sound.music();
    }

private:
    const RunOptions* options_;
    Sound* sound_;
    const Music* music_ = nullptr;
    std::unique_ptr<audio::AdlibDriver> adlib_;
    audio::Mt32Driver::Log log_;
    std::unique_ptr<audio::Mt32Driver> mt32_;
};

// The state hashes due at `tick`: those of the requested ticks reached.
void report_state(Machine& machine, std::uint32_t tick, const RunOptions& options, std::size_t& next) {
    if (!options.on_state_hash) {
        return;
    }
    for (; next < options.state_hash_ticks.size() && tick >= options.state_hash_ticks[next]; ++next) {
        options.on_state_hash(options.state_hash_ticks[next], tick, state_hash(machine));
    }
}

// The globals due at `tick`: those of the requested ticks reached.
void report_globals(const Machine& machine, std::uint32_t tick, const RunOptions& options, std::size_t& next) {
    if (!options.on_globals) {
        return;
    }
    for (; next < options.globals_ticks.size() && tick >= options.globals_ticks[next]; ++next) {
        std::vector<Reg> globals(machine.global_count());
        for (std::size_t i = 0; i < globals.size(); ++i) {
            globals[i] = machine.global(i);
        }
        options.on_globals(options.globals_ticks[next], tick, globals);
    }
}

// The save round trips due at `tick`: the state saved, then read into a
// fresh machine on the same game files.
void check_saves(const resource::ResourceManager& resources, Machine& machine, std::uint32_t tick,
                 const RunOptions& options, std::size_t& next) {
    if (!options.on_save_check) {
        return;
    }
    for (; next < options.save_check_ticks.size() && tick >= options.save_check_ticks[next]; ++next) {
        SaveCheck check;
        const std::vector<std::uint8_t> file = write_save(machine, "round trip", "sci-vm-trace");
        check.bytes = file.size();
        check.saved = state_hash(machine);
        Subsystems subsystems;
        subsystems.set_seed(options.seed);
        subsystems.files().set_store(options.files);
        Machine fresh(resources, subsystems);
        subsystems.attach(fresh);
        if (!fresh.init_game()) {
            check.error = fresh.faults().message();
        } else if (const auto restored = read_save(fresh, file); !restored) {
            check.error = restored.error().message;
        } else {
            check.restored = state_hash(fresh);
        }
        options.on_save_check(options.save_check_ticks[next], tick, check);
    }
}

} // namespace

namespace {

// A machine and its subsystems; the machine goes first.
struct Engine {
    std::unique_ptr<Subsystems> subsystems;
    std::unique_ptr<Machine> machine;
};

// A machine on the game files with the run's seed, files and saves.
Engine make_engine(const resource::ResourceManager& resources, const RunOptions& options) {
    Engine engine;
    engine.subsystems = std::make_unique<Subsystems>();
    engine.subsystems->set_seed(options.seed);
    engine.subsystems->files().set_store(options.files);
    engine.subsystems->files().set_saves(options.saves, "sci-vm-trace");
    engine.machine = std::make_unique<Machine>(resources, *engine.subsystems);
    return engine;
}

// Reads a save with its execution into a fresh, initialised machine.
core::Result<SaveHeader> resume_from(Engine& engine, std::span<const std::uint8_t> file) {
    engine.subsystems->attach(*engine.machine);
    if (!engine.machine->init_game()) {
        return core::make_error(core::ErrorCode::InvalidData, engine.machine->faults().message());
    }
    return read_save(*engine.machine, file, Execution::Resume);
}

} // namespace

core::Result<StateHash> load_save_hash(const resource::ResourceManager& resources, std::span<const std::uint8_t> file) {
    const RunOptions options;
    Engine engine = make_engine(resources, options);
    if (auto header = resume_from(engine, file); !header) {
        return core::make_error(header.error().code, header.error().message);
    }
    return state_hash(*engine.machine);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the hooks of every output.
RunResult run_deterministic(const resource::ResourceManager& resources, const RunOptions& options) {
    Engine engine = make_engine(resources, options);
    for (const InputEvent& event : options.input) {
        engine.subsystems->events().schedule(event);
    }
    // The machine running; a round trip replaces it.
    Machine* machine = engine.machine.get();
    Subsystems* subsystems = engine.subsystems.get();
    std::uint32_t calls = 0;
    // At the stop, the reference calls quitGame(): the game gets a quit
    // event at its next event poll. Should it never poll, it is stopped a
    // minute (of virtual time) or a million kernel calls later.
    const auto check_stop = [&] {
        const std::uint32_t ticks = subsystems->clock().ticks();
        if (ticks >= options.stop_tick || calls >= options.stop_calls) {
            subsystems->events().request_quit();
        }
        if (ticks >= options.stop_tick + std::uint64_t{3600} || calls >= options.stop_calls + std::uint64_t{1000000}) {
            machine->abort(Abort::QuitGame);
        }
    };
    std::size_t next_state_hash = 0;
    std::size_t next_globals = 0;
    std::size_t next_save_check = 0;
    std::size_t next_round_trip = 0;
    std::vector<CastEntry> cast;
    // The round trip due, saved at the resume point.
    std::optional<RoundTrip> round_trip;
    std::uint32_t round_trip_requested = 0;
    std::uint32_t round_trip_tick = 0;
    // The run's outputs, on the machine running.
    const auto hook = [&](Subsystems* parts, Machine* vm) {
        Clock* clock = &parts->clock();
        clock->set_advance_hook([&, parts, vm, clock] {
            check_stop();
            const std::uint32_t ticks = clock->ticks();
            report_state(*vm, ticks, options, next_state_hash);
            report_globals(*vm, ticks, options, next_globals);
            check_saves(resources, *vm, ticks, options, next_save_check);
            if (options.on_tick && parts->graphics().attached()) {
                Screen& screen = parts->graphics().screen();
                options.on_tick(ScreenMaps{
                    .tick = ticks,
                    .shown = screen.shown(),
                    .priority = screen.priority_map(),
                    .control = screen.control_map(),
                });
            }
        });
        parts->parser().set_recording(static_cast<bool>(options.on_parser));
        if (options.on_layout) {
            parts->graphics().set_layout_log(
                [&, clock](std::string_view line) { options.on_layout(calls, clock->ticks(), line); });
        }
        vm->set_kernel_log([&, parts, vm, clock](const KernelCall& call) {
            if (options.trace) {
                const SegManager* callers = options.trace_callers ? &vm->segments() : nullptr;
                options.trace(trace_line(call, calls, clock->ticks(), callers));
            }
            if (options.on_animate && std::string_view(call.name) == "Animate") {
                report_cast(parts->graphics().animate(), cast);
                options.on_animate(calls, clock->ticks(), cast);
            }
            report_parser(*vm, parts->parser(), call, calls, clock->ticks(), options);
            ++calls;
            check_stop();
        });
        if (!options.round_trip_ticks.empty()) {
            vm->set_suspend_hook([&, vm, clock] {
                const std::uint32_t ticks = clock->ticks();
                const std::vector<std::uint32_t>& due = options.round_trip_ticks;
                if (next_round_trip >= due.size() || ticks < due[next_round_trip]) {
                    return false;
                }
                round_trip_requested = due[next_round_trip];
                round_trip_tick = ticks;
                while (next_round_trip < due.size() && ticks >= due[next_round_trip]) {
                    ++next_round_trip;
                }
                round_trip.emplace();
                round_trip->call = calls;
                round_trip->file = write_save(*vm, "round trip", "sci-vm-trace");
                round_trip->saved = state_hash(*vm);
                return true;
            });
        }
    };
    hook(subsystems, machine);
    MusicLog music_log(options, subsystems->sound());
    if (auto error = music_log.open(resources)) {
        return RunResult{.fault = std::move(error), .ticks = 0, .calls = 0};
    }
    if (!machine->faults().raised()) {
        subsystems->attach(*machine);
        music_log.attached();
        if (machine->init_game()) {
            machine->run_game();
        }
    }
    // Each round trip: the run goes on in a fresh machine.
    std::vector<Engine> stopped;
    while (machine->suspended() && round_trip) {
        Engine fresh = make_engine(resources, options);
        if (auto restored = resume_from(fresh, round_trip->file); !restored) {
            round_trip->error = restored.error().message;
        } else {
            round_trip->restored = state_hash(*fresh.machine);
        }
        if (options.on_round_trip) {
            options.on_round_trip(round_trip_requested, round_trip_tick, *round_trip);
        }
        if (round_trip->error) {
            return RunResult{
                .fault = "round trip: " + *round_trip->error,
                .ticks = subsystems->clock().ticks(),
                .calls = calls,
            };
        }
        round_trip.reset();
        // What is not VM state goes over as the host keeps it: the input
        // not yet due, the music device, what the screen shows.
        for (const InputEvent& event : subsystems->events().pending()) {
            fresh.subsystems->events().schedule(event);
        }
        if (subsystems->events().quit_requested()) {
            fresh.subsystems->events().request_quit();
        }
        music_log.move_to(fresh.subsystems->sound());
        fresh.subsystems->graphics().screen().take_presentation(subsystems->graphics().screen());
        // The machine that stopped stays, without its hooks, until the run
        // ends: its parts hold no reference into the fresh one.
        subsystems->clock().set_advance_hook(nullptr);
        machine->set_kernel_log(nullptr);
        machine->set_suspend_hook(nullptr);
        stopped.push_back(std::move(engine));
        engine = std::move(fresh);
        machine = engine.machine.get();
        subsystems = engine.subsystems.get();
        hook(subsystems, machine);
        machine->resume_game();
    }
    RunResult result{.fault = std::nullopt, .ticks = subsystems->clock().ticks(), .calls = calls};
    if (machine->faults().raised()) {
        result.fault = machine->faults().message();
    }
    return result;
}

} // namespace sci::vm
