// SPDX-License-Identifier: GPL-3.0-or-later

#include "subsystems.hpp"

#include "events.hpp"
#include "files.hpp"
#include "graphics.hpp"
#include "parser.hpp"
#include "sound.hpp"
#include "timing.hpp"

namespace sci::vm {

Subsystems::Subsystems()
    : clock_(std::make_unique<Clock>()), random_(std::make_unique<RandomSource>()),
      events_(std::make_unique<EventQueue>()), graphics_(std::make_unique<Graphics>()),
      sound_(std::make_unique<Sound>()), parser_(std::make_unique<Parser>()), files_(std::make_unique<Files>()) {
}

Subsystems::~Subsystems() = default;

void Subsystems::attach(Machine& machine) {
    graphics_->attach(machine);
    sound_->attach(machine);
    parser_->attach(machine);
    files_->attach(machine);
}

void Subsystems::on_init_game(Machine& /*machine*/) {
    // SciEngine::initGame: the wait and throttle times start now, and the
    // random source is seeded.
    clock_->reset_wait();
    random_->set_seed(seed_);
    parser_->reset();
}

void Subsystems::on_stop_game(Machine& /*machine*/) {
    // SciEngine::runGame stops the sounds unless a game is being restored.
    sound_->clear_play_list();
}

void Subsystems::on_restart_game(Machine& /*machine*/) {
    // SciEngine::runGame after a restart.
    clock_->reset_throttle();
    graphics_->on_restart_game();
}

std::vector<Reg> Subsystems::engine_hunks() const {
    return graphics_->engine_hunks();
}

} // namespace sci::vm
