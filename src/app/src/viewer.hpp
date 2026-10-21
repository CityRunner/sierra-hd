// SPDX-License-Identifier: GPL-3.0-or-later
//
// The room viewer of release v0.1 (spec section 12, milestone 3a): pictures
// in HD or original mode, views with their animation, and fonts, composed
// into a 320N x 200N frame. It knows nothing about the window: the window
// layer passes commands and the window size in, and uploads the changed
// rectangles of the frame (spec section 10.1).
#pragma once

#include "sci/core/executor.hpp"
#include "sci/platform/config.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/resource/resource_manager.hpp"
#include "sci/text/font.hpp"
#include "sci/view/view.hpp"

#include <cstdint>
#include <filesystem>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace sci::app {

inline constexpr int screen_width = 320;
inline constexpr int screen_height = 200;
inline constexpr int menu_height = 10;

// A rectangle of the frame, in frame pixels.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class Browser : std::int8_t { Pictures, Views, Fonts };

enum class Command : std::int8_t {
    Next, // Next picture, view or font.
    Previous,
    NextLoop, // Views: next loop.
    PreviousLoop,
    NextBrowser,    // Pictures, views, fonts.
    ToggleMode,     // HD or original.
    TogglePriority, // Pictures: the visual or the priority map.
    ScaleUp,
    ScaleDown,
    ScaleAuto,
    ToggleAnimation,
};

class Viewer {
public:
    // `hints` is the directory of the hint files; none if it does not exist.
    Viewer(const resource::ResourceManager& manager, std::optional<std::filesystem::path> hints,
           core::Executor& executor, platform::GraphicsMode mode, int scale);

    // The window's drawable size in physical pixels; decides N.
    void set_window_size(int width, int height);

    void handle(Command command);

    // Advances the view animation by `milliseconds`.
    void advance(int milliseconds);

    [[nodiscard]] int scale() const { return scale_; }

    [[nodiscard]] platform::GraphicsMode mode() const { return mode_; }

    // N as configured: 0 for the largest that fits.
    [[nodiscard]] int chosen_scale() const { return chosen_scale_; }

    // The frame, 320N x 200N RGBA, up to date after take_dirty().
    [[nodiscard]] const std::vector<std::uint8_t>& frame();

    // Whether the frame changed since the last take_dirty().
    [[nodiscard]] bool has_changes() const { return full_redraw_ || !dirty_.empty(); }

    // The rectangles changed since the last call. A change of N redraws the
    // whole frame.
    [[nodiscard]] std::vector<Rect> take_dirty();

private:
    void redraw();
    void draw_status();
    void draw_background();
    void draw_view_cel(bool erase_previous);
    void draw_font();
    static void step(const std::vector<std::uint16_t>& numbers, std::size_t& index, int direction);
    [[nodiscard]] const render::RgbaImage& background();
    [[nodiscard]] std::string status_text() const;
    void fill(Rect rect, std::uint8_t color);
    void blit_indices(const std::vector<std::uint8_t>& indices, int width, int height, int x, int y,
                      std::optional<std::uint8_t> transparent);
    void mark(Rect rect);

    const resource::ResourceManager* manager_;
    std::optional<std::filesystem::path> hints_;
    core::Executor* executor_;
    platform::GraphicsMode mode_;
    int chosen_scale_;
    int scale_ = 1;
    int fit_scale_ = 1;

    Browser browser_ = Browser::Pictures;
    bool priority_ = false;
    bool animate_ = true;
    std::vector<std::uint16_t> pictures_;
    std::vector<std::uint16_t> views_;
    std::vector<std::uint16_t> fonts_;
    std::size_t picture_ = 0;
    std::size_t view_ = 0;
    std::size_t font_ = 0;
    std::size_t loop_ = 0;
    std::size_t cel_ = 0;
    int cel_time_ = 0;
    std::optional<view::View> view_data_;
    std::optional<text::Font> status_font_;
    std::optional<text::Font> font_data_;
    std::string message_; // Why the shown resource could not be drawn.
    std::optional<Rect> cel_rect_;

    struct CachedBackground {
        std::uint16_t picture = 0;
        platform::GraphicsMode mode = platform::GraphicsMode::Hd;
        bool priority = false;
        int scale = 0;
        render::RgbaImage image;
    };

    std::list<CachedBackground> cache_; // Most recent first.

    std::vector<std::uint8_t> frame_;
    std::vector<Rect> dirty_;
    bool full_redraw_ = true;
};

} // namespace sci::app
