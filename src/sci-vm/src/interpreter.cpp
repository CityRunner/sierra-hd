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
#include "sci/audio/adlib.hpp"
#include "sci/audio/mt32.hpp"
#include "sci/audio/mt32_driver.hpp"
#include "sound.hpp"
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
            mt32_ = std::make_unique<audio::Mt32Driver>(options_->mt32(), *patch,
                                                        options_->on_midi ? &log_ : nullptr);
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

private:
    const RunOptions* options_;
    Sound* sound_;
    const Music* music_ = nullptr;
    std::unique_ptr<audio::AdlibDriver> adlib_;
    audio::Mt32Driver::Log log_;
    std::unique_ptr<audio::Mt32Driver> mt32_;
};

} // namespace

RunResult run_deterministic(const resource::ResourceManager& resources, const RunOptions& options) {
    Subsystems subsystems;
    subsystems.set_seed(options.seed);
    for (const InputEvent& event : options.input) {
        subsystems.events().schedule(event);
    }
    subsystems.files().set_store(options.files);
    Machine machine(resources, subsystems);
    Clock& clock = subsystems.clock();
    std::uint32_t calls = 0;
    // At the stop, the reference calls quitGame(): the game gets a quit
    // event at its next event poll. Should it never poll, it is stopped a
    // minute (of virtual time) or a million kernel calls later.
    const auto check_stop = [&] {
        if (clock.ticks() >= options.stop_tick || calls >= options.stop_calls) {
            subsystems.events().request_quit();
        }
        if (clock.ticks() >= options.stop_tick + std::uint64_t{3600} ||
            calls >= options.stop_calls + std::uint64_t{1000000}) {
            machine.abort(Abort::QuitGame);
        }
    };
    clock.set_advance_hook([&] {
        check_stop();
        if (options.on_tick && subsystems.graphics().attached()) {
            Screen& screen = subsystems.graphics().screen();
            options.on_tick(ScreenMaps{
                .tick = clock.ticks(),
                .shown = screen.shown(),
                .priority = screen.priority_map(),
                .control = screen.control_map(),
            });
        }
    });
    std::vector<CastEntry> cast;
    subsystems.parser().set_recording(static_cast<bool>(options.on_parser));
    if (options.on_layout) {
        subsystems.graphics().set_layout_log(
            [&](std::string_view line) { options.on_layout(calls, clock.ticks(), line); });
    }
    machine.set_kernel_log([&](const KernelCall& call) {
        if (options.trace) {
            const SegManager* callers = options.trace_callers ? &machine.segments() : nullptr;
            options.trace(trace_line(call, calls, clock.ticks(), callers));
        }
        if (options.on_animate && std::string_view(call.name) == "Animate") {
            report_cast(subsystems.graphics().animate(), cast);
            options.on_animate(calls, clock.ticks(), cast);
        }
        report_parser(machine, subsystems.parser(), call, calls, clock.ticks(), options);
        ++calls;
        check_stop();
    });
    MusicLog music_log(options, subsystems.sound());
    if (auto error = music_log.open(resources)) {
        return RunResult{.fault = std::move(error), .ticks = 0, .calls = 0};
    }
    if (!machine.faults().raised()) {
        subsystems.attach(machine);
        music_log.attached();
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
