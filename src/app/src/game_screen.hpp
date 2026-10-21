// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's frame (milestone 4b): what the interpreter shows, composed
// into a 320N x 200N RGBA frame in original or HD mode, with the changed
// rectangles for the window layer (spec sections 10.1 and 6.9). In HD mode
// the picture comes from the vector path, and every 1x pixel that differs
// from the picture as drawn (sprites, text, windows, the menu bar) is drawn
// as an N x N block over it. The window layer does the cursor and input.
#pragma once

#include "sci/platform/config.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vm/events.hpp"
#include "sci/vm/game.hpp"
#include "viewer.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace sci::app {

// The HD picture, 320N x 190N, or none if it cannot be drawn.
using HdBackground = std::function<const render::RgbaImage*(int picture, bool mirrored, int ega_palette, int scale)>;

class GameScreen {
public:
    explicit GameScreen(HdBackground background) : background_(std::move(background)) {}

    // Brings the frame up to date with `shown`; the frame changes only where
    // the shown screen, the mode or N changed.
    void update(const vm::ShownScreen& shown, platform::GraphicsMode mode, int scale);

    [[nodiscard]] const std::vector<std::uint8_t>& frame() const { return frame_; }

    [[nodiscard]] int scale() const { return scale_; }

    // The rectangles changed since the last call.
    [[nodiscard]] std::vector<Rect> take_dirty();

private:
    void draw_pixel(const vm::ShownScreen& shown, const render::RgbaImage* background, int x, int y);

    HdBackground background_;
    std::vector<std::uint8_t> frame_;
    std::vector<std::uint8_t> last_visual_;
    std::vector<std::uint8_t> last_cover_;
    int last_picture_ = -2;
    bool last_mirrored_ = false;
    int last_ega_palette_ = 0;
    platform::GraphicsMode mode_ = platform::GraphicsMode::Original;
    int scale_ = 0;
    std::vector<Rect> dirty_;
};

// An SCI0 cursor resource as RGBA pixels (16 x 16) and its hotspot.
struct CursorImage {
    std::array<std::uint8_t, 16 * 16 * 4> pixels{};
    int hotspot_x = 0;
    int hotspot_y = 0;
};

[[nodiscard]] std::optional<CursorImage> decode_cursor(std::span<const std::uint8_t> resource);

// A key press as the window layer sees it, made neutral of SDL.
struct KeyPress {
    enum class Key : std::uint8_t {
        Character, // `character` holds the ASCII character (with Shift applied).
        Enter,
        Escape,
        Backspace,
        Tab,
        Up,
        Down,
        Left,
        Right,
        Home,
        End,
        PageUp,
        PageDown,
        Insert,
        Delete,
        Center,   // Keypad 5.
        Function, // F1 to F10 in `function`.
        Other,
    };
    Key key = Key::Other;
    char character = 0;
    int function = 0;
    bool keypad = false; // Keypad digits, period, enter and operators.
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool num_lock = false;
};

// EventManager::getScummVMEvent for a key: the SCI event, or none.
[[nodiscard]] std::optional<vm::SciEvent> sci_key_event(const KeyPress& key);

} // namespace sci::app
