// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parts of the engine around the bytecode interpreter that kernel
// functions use: the virtual clock and random source, events, graphics at
// 1x, sound and the parser. In ScummVM these are SciEngine's members.

#pragma once

#include "sci/vm/reg.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace sci::vm {

class Machine;
class Clock;
class RandomSource;
class EventQueue;
class Graphics;
class Sound;
class Parser;
class Files;

class Subsystems {
public:
    Subsystems();
    ~Subsystems();
    Subsystems(const Subsystems&) = delete;
    Subsystems& operator=(const Subsystems&) = delete;
    Subsystems(Subsystems&&) = delete;
    Subsystems& operator=(Subsystems&&) = delete;

    // Creates the engine parts once the machine has its resources
    // (SciEngine::run, before initGame).
    void attach(Machine& machine);

    // Hooks of SciEngine::initGame and runGame.
    void on_init_game(Machine& machine);
    void on_stop_game(Machine& machine);
    void on_restart_game(Machine& machine);
    // Hunks the engine holds for itself (GfxPorts::processEngineHunkList).
    [[nodiscard]] std::vector<Reg> engine_hunks() const;

    // The seed of the random source, set at every initGame.
    void set_seed(std::uint32_t seed) { seed_ = seed; }

    // EngineState::_eventCounter: kGetEvent calls since kWait or
    // kGameIsRestarting(0).
    void reset_event_counter() { event_counter_ = 0; }

    int count_event() { return ++event_counter_; }

    [[nodiscard]] Clock& clock() { return *clock_; }

    [[nodiscard]] RandomSource& random() { return *random_; }

    [[nodiscard]] EventQueue& events() { return *events_; }

    [[nodiscard]] Graphics& graphics() { return *graphics_; }

    [[nodiscard]] Sound& sound() { return *sound_; }

    [[nodiscard]] Parser& parser() { return *parser_; }

    [[nodiscard]] Files& files() { return *files_; }

private:
    std::unique_ptr<Clock> clock_;
    std::unique_ptr<RandomSource> random_;
    std::unique_ptr<EventQueue> events_;
    std::unique_ptr<Graphics> graphics_;
    std::unique_ptr<Sound> sound_;
    std::unique_ptr<Parser> parser_;
    std::unique_ptr<Files> files_;
    std::uint32_t seed_ = 0;
    int event_counter_ = 0;
};

} // namespace sci::vm
