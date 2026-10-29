// SPDX-License-Identifier: GPL-3.0-or-later

#include "subsystems.hpp"

#include "events.hpp"
#include "files.hpp"
#include "graphics.hpp"
#include "machine.hpp"
#include "music.hpp"
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
    events_->set_update_screen([this, &machine] { update_screen(machine); });
}

void Subsystems::on_init_game(Machine& /*machine*/) {
    // SciEngine::initGame: the wait and throttle times start now, and the
    // random source is seeded.
    clock_->reset_wait();
    screen_update_time_ = clock_->millis();
    random_->set_seed(seed_);
    parser_->reset();
}

void Subsystems::on_stop_game(Machine& /*machine*/) {
    // SciEngine::runGame stops the sounds unless a game is being restored.
    sound_->clear_play_list();
    files_->close_all();
}

void Subsystems::on_restart_game(Machine& /*machine*/) {
    // SciEngine::runGame after a restart.
    clock_->reset_throttle();
    graphics_->on_restart_game();
}

Subsystems::HostState Subsystems::host_state() {
    const Music::HostSettings music = sound_->has_music() ? sound_->music().host_settings() : Music::HostSettings();
    return HostState{
        .time = clock_->host_time(),
        .random_seed = random_->seed(),
        .timer_calls = sound_->has_music() ? sound_->music().timer_calls() : 0,
        .master_volume = sound_->has_music() ? sound_->music().master_volume() : std::uint16_t{0},
        .music = music,
    };
}

void Subsystems::after_restore(const HostState& host) {
    clock_->after_restore(host.time);
    random_->set_seed(host.random_seed);
    if (sound_->has_music()) {
        sound_->music().set_timer_calls(host.timer_calls);
    }
    screen_update_time_ = clock_->millis();
    event_counter_ = 0;
}

void Subsystems::update_screen(Machine& machine) {
    constexpr std::uint32_t interval = 1000 / 60;
    if (clock_->millis() - screen_update_time_ >= interval) {
        screen_update_time_ = clock_->millis();
        if (events_->quit_requested()) {
            machine.abort(Abort::QuitGame);
        }
    }
}

std::vector<Reg> Subsystems::engine_hunks() const {
    return graphics_->engine_hunks();
}

} // namespace sci::vm
