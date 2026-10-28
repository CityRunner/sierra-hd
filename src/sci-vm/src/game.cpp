// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vm/game.hpp"

#include "events.hpp"
#include "files.hpp"
#include "graphics.hpp"
#include "machine.hpp"
#include "sound.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

namespace sci::vm {

struct Game::State {
    State(const resource::ResourceManager& resources, std::uint32_t seed)
        : machine(resources, init(subsystems, seed)) {}

    static Subsystems& init(Subsystems& subsystems, std::uint32_t seed) {
        subsystems.set_seed(seed);
        return subsystems;
    }

    Subsystems subsystems;
    Machine machine;
};

Game::Game(const resource::ResourceManager& resources, GameHost& host, FileStore* files, std::uint32_t seed)
    : state_(std::make_unique<State>(resources, seed)) {
    state_->subsystems.files().set_store(files);
    state_->subsystems.clock().set_wait_hook(
        [this, &host](std::uint32_t milliseconds) { host.wait(*this, milliseconds); });
    state_->subsystems.clock().set_local_time([&host] { return host.local_time(); });
}

Game::~Game() = default;

void Game::set_music_device(audio::MidiDevice* device) {
    state_->subsystems.sound().set_device(device);
}

std::optional<std::string> Game::run() {
    Machine& machine = state_->machine;
    if (!machine.faults().raised()) {
        state_->subsystems.attach(machine);
        if (machine.init_game()) {
            machine.run_game();
        }
    }
    if (machine.faults().raised()) {
        return machine.faults().message();
    }
    return std::nullopt;
}

ShownScreen Game::screen() const {
    Graphics& graphics = state_->subsystems.graphics();
    ShownScreen shown;
    if (!graphics.attached()) {
        return shown;
    }
    const Screen& screen = graphics.screen();
    shown.visual = screen.shown();
    shown.serial = screen.shown_serial();
    shown.owner = screen.shown_owner();
    shown.background_visual = screen.shown_background_visual();
    shown.background_owner = screen.shown_background_owner();
    shown.background_priority = screen.shown_background_priority();
    shown.background_source = screen.shown_background_source();
    shown.sprite_layer = screen.shown_sprite_layer();
    shown.sprite_lists = screen.sprite_lists();
    shown.pictures = graphics.paint().shown_stack();
    shown.lines = graphics.paint().shown_lines();
    const Cursor& cursor = graphics.cursor();
    shown.cursor = cursor.visible() ? cursor.shape() : -1;
    shown.mouse = state_->subsystems.events().mouse();
    return shown;
}

void Game::input(const SciEvent& event) {
    state_->subsystems.events().push(event);
}

void Game::schedule(std::span<const InputEvent> events) {
    for (const InputEvent& event : events) {
        state_->subsystems.events().schedule(event);
    }
}

void Game::move_mouse(Point position) {
    state_->subsystems.events().set_mouse(position);
}

void Game::pass_wait(std::uint32_t milliseconds) {
    state_->subsystems.clock().run_music_in_wait(milliseconds);
}

void Game::quit() {
    state_->subsystems.events().request_quit();
}

} // namespace sci::vm
