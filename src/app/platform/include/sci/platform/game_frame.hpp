// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's frame (milestone 5, docs/hd-composition.md): what the
// interpreter shows, composed into a 320N x 200N RGBA frame in original or
// HD mode (sci/render/compose.hpp), with the changed rectangles for the
// window layer (spec sections 10.1 and 10.3). Only the 1x pixels whose
// shown data changed are composed again, in tiles; everything when the
// mode, N, the snapping or the picture changes. The program and the tools
// share it.
#pragma once

#include "sci/platform/config.hpp"
#include "sci/platform/hd_pictures.hpp"
#include "sci/render/compose.hpp"
#include "sci/vm/game.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace sci::platform {

// The HD images of a picture stack with its kernel lines, at N.
using HdSource =
    std::function<HdImages(std::span<const vm::ShownPicture> stack, std::span<const vm::ShownLine> lines, int scale)>;

class GameFrame {
public:
    explicit GameFrame(HdSource source) : source_(std::move(source)) {}

    // Brings the frame up to date with `shown`.
    void update(const vm::ShownScreen& shown, GraphicsMode mode, int scale, bool snap);

    [[nodiscard]] const std::vector<std::uint8_t>& frame() const { return frame_; }

    [[nodiscard]] int scale() const { return scale_; }

    // The rectangles of the frame changed since the last call, in frame
    // pixels.
    [[nodiscard]] std::vector<render::PixelRect> take_dirty();

    // The 1x pixels composed by the last update.
    [[nodiscard]] std::size_t last_composed() const { return last_composed_; }

private:
    void remember(const vm::ShownScreen& shown);

    HdSource source_;
    std::vector<std::uint8_t> frame_;
    // What the frame shows.
    std::vector<std::uint8_t> visual_;
    std::vector<std::uint8_t> owner_;
    std::vector<std::uint8_t> background_visual_;
    std::vector<std::uint8_t> background_owner_;
    std::vector<std::uint8_t> background_priority_;
    std::vector<std::uint8_t> background_source_;
    std::vector<std::uint32_t> sprite_layer_;
    std::vector<vm::ShownPicture> pictures_;
    std::vector<vm::ShownLine> lines_;
    GraphicsMode mode_ = GraphicsMode::Original;
    int scale_ = 0;
    bool snap_ = false;
    std::vector<render::PixelRect> dirty_;
    std::size_t last_composed_ = 0;
};

} // namespace sci::platform
