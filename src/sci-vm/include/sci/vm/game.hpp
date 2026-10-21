// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the game for a player (spec section 7): the interpreter runs on the
// caller's thread, and wherever virtual time passes (kWait, the speed
// throttle, the event poll delay) it calls the host, which waits that long
// in real time while it presents the screen and passes input on. The game
// loop thus runs at the original tick rate.

#pragma once

#include "sci/resource/resource_manager.hpp"
#include "sci/vm/events.hpp"
#include "sci/vm/files.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace sci::vm {

class Game;

class GameHost {
public:
    GameHost() = default;
    GameHost(const GameHost&) = delete;
    GameHost& operator=(const GameHost&) = delete;
    GameHost(GameHost&&) = delete;
    GameHost& operator=(GameHost&&) = delete;
    virtual ~GameHost() = default;

    // Virtual time is about to pass by `milliseconds`: present `game`'s
    // screen, pass input on, and return once that much real time has passed.
    virtual void wait(Game& game, std::uint32_t milliseconds) = 0;
};

// What the player sees, in 1x.
struct ShownScreen {
    // 320 x 200 EGA colours, as last copied to the screen.
    std::span<const std::uint8_t> visual;
    // Counts the changes of `visual`.
    std::uint64_t serial = 0;
    // The picture shown, as drawn without later additions, and the visual
    // map it left: where `visual` differs, something is drawn over it.
    int picture = -1;
    bool mirrored = false;
    int ega_palette = 0;
    std::span<const std::uint8_t> picture_visual;
    // The mouse cursor: its resource, or -1 when hidden; the mouse in 1x.
    int cursor = -1;
    Point mouse;
};

class Game {
public:
    Game(const resource::ResourceManager& resources, GameHost& host, FileStore* files, std::uint32_t seed);
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    Game(Game&&) = delete;
    Game& operator=(Game&&) = delete;

    // Runs until the game quits; the fault that stopped it, if any.
    std::optional<std::string> run();

    [[nodiscard]] ShownScreen screen() const;
    // Input now. Mouse events carry their position in 1x.
    void input(const SciEvent& event);
    void move_mouse(Point position);
    // Ends the game at its next event poll, as quitting ScummVM does.
    void quit();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace sci::vm
