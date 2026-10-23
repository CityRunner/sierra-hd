// SPDX-License-Identifier: GPL-3.0-or-later
//
// The checks of milestone 6b on a composed game frame
// (docs/hd-composition.md, "The interface at every N"): windows, text,
// menus and the menu bar are the shown 1x screen scaled N x N, on top of
// everything, in original and HD mode. sci-hd-frames runs them on the
// frames of the scenarios, the unit tests on synthetic screens.

#pragma once

#include "sci/vm/game.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sci::tools {

// The first 1x pixel a check found wrong, and why.
struct FrameProblem {
    int x = 0;
    int y = 0;
    std::string what;
};

// Every pixel the interface owns (with `hd` false: every pixel) is the
// N x N block of its shown EGA colour in `frame` (320N x 200N RGBA).
[[nodiscard]] std::optional<FrameProblem>
check_interface_blocks(const vm::ShownScreen& shown, std::span<const std::uint8_t> frame, int scale, bool hd);

// The menu bar's rows (0 to 9) are interface.
[[nodiscard]] std::optional<FrameProblem> check_menu_bar(const vm::ShownScreen& shown);

// The shown visual map with every interface pixel in another EGA colour.
[[nodiscard]] std::vector<std::uint8_t> recolour_interface(const vm::ShownScreen& shown);

// `frame` composes `shown`, `recoloured` the same screen with the visual
// map of recolour_interface(): the blocks outside the interface are the
// same in both (no window bleeds into the picture, the picture does not
// depend on what the windows show), and the interface blocks show the new
// colours.
[[nodiscard]] std::optional<FrameProblem> check_isolation(const vm::ShownScreen& shown,
                                                          std::span<const std::uint8_t> frame,
                                                          std::span<const std::uint8_t> recoloured, int scale);

// Whether any interface pixel below the menu bar covers an opaque pixel of
// a sprite shown on the screen: a window over a sprite.
[[nodiscard]] bool window_over_sprite(const vm::ShownScreen& shown);

} // namespace sci::tools
