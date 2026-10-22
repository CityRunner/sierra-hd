// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/event.cpp (getScummVMEvent: the key translation, altify),
//   engines/sci/graphics/cursor.cpp (kernelSetShape: the SCI0 cursor format)
// and translated into this project's style, types and error handling.
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "game_screen.hpp"

#include "sci/render/colors.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace sci::app {

namespace {

constexpr int picture_top = 10; // The picture's first screen row.

constexpr std::uint16_t key_shift_tab = 15U << 8U;
constexpr std::uint16_t key_home = 71U << 8U;
constexpr std::uint16_t key_up = 72U << 8U;
constexpr std::uint16_t key_page_up = 73U << 8U;
constexpr std::uint16_t key_left = 75U << 8U;
constexpr std::uint16_t key_center = 76U << 8U;
constexpr std::uint16_t key_right = 77U << 8U;
constexpr std::uint16_t key_end = 79U << 8U;
constexpr std::uint16_t key_down = 80U << 8U;
constexpr std::uint16_t key_page_down = 81U << 8U;
constexpr std::uint16_t key_insert = 82U << 8U;
constexpr std::uint16_t key_delete = 83U << 8U;
constexpr std::uint16_t key_f1 = 59U << 8U;
constexpr std::uint16_t key_shift_f1 = 84U << 8U;

// The IBM scancode of an Alt key (altify): rows of the keyboard.
std::uint16_t altify(char character) {
    struct Row {
        unsigned offset;
        std::string_view keys;
    };

    constexpr std::array<Row, 3> rows{
        {
            {.offset = 0x10, .keys = "QWERTYUIOP[]"},
            {.offset = 0x1E, .keys = "ASDFGHJKL;'\\"},
            {.offset = 0x2C, .keys = "ZXCVBNM,./"},
        },
    };
    const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    for (const Row& row : rows) {
        if (const auto at = row.keys.find(upper); at != std::string_view::npos) {
            return static_cast<std::uint16_t>((row.offset + at) << 8U);
        }
    }
    return static_cast<std::uint8_t>(character);
}

// The key without Num Lock, and the character with it (keyMappings).
struct Mapping {
    KeyPress::Key key;
    std::uint16_t code;
};

constexpr std::array<Mapping, 11> key_mappings{
    {
        {.key = KeyPress::Key::Up, .code = key_up},
        {.key = KeyPress::Key::Down, .code = key_down},
        {.key = KeyPress::Key::Left, .code = key_left},
        {.key = KeyPress::Key::Right, .code = key_right},
        {.key = KeyPress::Key::Insert, .code = key_insert},
        {.key = KeyPress::Key::Home, .code = key_home},
        {.key = KeyPress::Key::End, .code = key_end},
        {.key = KeyPress::Key::PageUp, .code = key_page_up},
        {.key = KeyPress::Key::PageDown, .code = key_page_down},
        {.key = KeyPress::Key::Delete, .code = key_delete},
        {.key = KeyPress::Key::Center, .code = key_center},
    },
};

// The modifier bits of the key press.
std::uint16_t key_modifiers(const KeyPress& key) {
    std::uint16_t modifiers = vm::modifier_none;
    if (key.alt) {
        modifiers |= vm::modifier_alt;
    }
    if (key.ctrl) {
        modifiers |= vm::modifier_ctrl;
    }
    if (key.shift) {
        modifiers |= vm::modifier_shift;
    }
    return modifiers;
}

// A navigation key (or a keypad digit with Num Lock): its character or scancode.
std::uint16_t navigation_character(const KeyPress& key) {
    if (key.keypad && key.num_lock && key.character != 0) {
        return static_cast<std::uint8_t>(key.character); // Keypad digits and the period.
    }
    std::uint16_t character = 0;
    for (const Mapping& mapping : key_mappings) {
        if (mapping.key == key.key) {
            character = mapping.code;
        }
    }
    return character;
}

// The character of the key before Alt and Ctrl; nothing for a key the game
// does not see.
std::optional<std::uint16_t> base_character(const KeyPress& key) {
    switch (key.key) {
    case KeyPress::Key::Character: {
        const std::uint16_t character = static_cast<std::uint8_t>(key.character);
        if (character >= 0x80) {
            return std::nullopt; // This game's fonts have no extended characters.
        }
        return character;
    }
    case KeyPress::Key::Enter:
        return vm::key_enter;
    case KeyPress::Key::Escape:
        return vm::key_escape;
    case KeyPress::Key::Backspace:
        return 8;
    case KeyPress::Key::Tab:
        return key.shift ? key_shift_tab : '\t';
    case KeyPress::Key::Function:
        if (key.function < 1 || key.function > 10) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>((key.shift ? key_shift_f1 : key_f1) +
                                          (static_cast<unsigned>(key.function - 1) << 8U));
    case KeyPress::Key::Other:
        return std::nullopt;
    default:
        return navigation_character(key);
    }
}

// Alt and Ctrl applied to the character.
std::uint16_t apply_alt_ctrl(const KeyPress& key, std::uint16_t character) {
    if (key.alt && character > 0 && character < 27) {
        character += 96;
    }
    if (key.alt) {
        character = altify(static_cast<char>(character & 0xFFU));
    } else if (key.ctrl && !key.shift && character >= 'a' && character <= 'z') {
        character -= 96; // Ctrl+letter is the ASCII control character.
    }
    return character;
}

} // namespace

std::optional<vm::SciEvent> sci_key_event(const KeyPress& key) {
    const std::optional<std::uint16_t> base = base_character(key);
    if (!base) {
        return std::nullopt;
    }
    const std::uint16_t character = apply_alt_ctrl(key, *base);
    if (character == 0) {
        return std::nullopt;
    }
    const std::uint16_t modifiers = key_modifiers(key);
    return vm::SciEvent{.type = vm::event_key_down, .modifiers = modifiers, .character = character, .mouse = {}};
}

std::optional<CursorImage> decode_cursor(std::span<const std::uint8_t> resource) {
    constexpr int size = 16;
    if (resource.size() != 68) {
        return std::nullopt;
    }
    CursorImage image;
    // SCI0: the hotspot is centred if bit 0 of byte 3 is set, else top left.
    image.hotspot_x = image.hotspot_y = resource[3] != 0 ? size / 2 : 0;
    const auto word = [&](std::size_t at) {
        return static_cast<unsigned>(resource[at]) | (static_cast<unsigned>(resource[at + 1]) << 8U);
    };
    for (int y = 0; y < size; ++y) {
        const unsigned mask_a = word(4 + (static_cast<std::size_t>(y) * 2));
        const unsigned mask_b = word(4 + 32 + (static_cast<std::size_t>(y) * 2));
        for (int x = 0; x < size; ++x) {
            const unsigned color = (((mask_a << static_cast<unsigned>(x)) & 0x8000U) |
                                    (((mask_b << static_cast<unsigned>(x)) >> 1U) & 0x4000U)) >>
                                   14U;
            const std::size_t i = ((static_cast<std::size_t>(y) * size) + static_cast<std::size_t>(x)) * 4;
            // 0 black, 1 and 3 white, 2 transparent.
            const bool transparent = color == 2;
            const std::uint8_t value = color == 0 ? 0 : 0xFF;
            image.pixels[i] = value;
            image.pixels[i + 1] = value;
            image.pixels[i + 2] = value;
            image.pixels[i + 3] = transparent ? 0 : 0xFF;
        }
    }
    return image;
}

void GameScreen::draw_pixel(const vm::ShownScreen& shown, const render::RgbaImage* background, int x, int y) {
    const int width = screen_width * scale_;
    const std::size_t at = (static_cast<std::size_t>(y) * screen_width) + static_cast<std::size_t>(x);
    const std::uint8_t color = shown.visual[at];
    const bool from_picture =
        background != nullptr && y >= picture_top && at < shown.cover.size() && shown.cover[at] == 0;
    const render::Rgb8 rgb = render::ega_palette[color & 0x0FU];
    for (int dy = 0; dy < scale_; ++dy) {
        const int row = (y * scale_) + dy;
        for (int dx = 0; dx < scale_; ++dx) {
            const int column = (x * scale_) + dx;
            const std::size_t i =
                ((static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(column)) *
                4;
            if (from_picture) {
                const std::size_t j = ((static_cast<std::size_t>(row - (picture_top * scale_)) *
                                        static_cast<std::size_t>(background->width)) +
                                       static_cast<std::size_t>(column)) *
                                      4;
                std::copy_n(background->pixels.begin() + static_cast<std::ptrdiff_t>(j), 4,
                            frame_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                frame_[i] = rgb.r;
                frame_[i + 1] = rgb.g;
                frame_[i + 2] = rgb.b;
                frame_[i + 3] = 0xFF;
            }
        }
    }
}

void GameScreen::update(const vm::ShownScreen& shown, platform::GraphicsMode mode, int scale) {
    const std::size_t pixels = static_cast<std::size_t>(screen_width) * screen_height;
    if (shown.visual.size() != pixels || shown.cover.size() != pixels) {
        return; // Nothing shown yet.
    }
    const render::RgbaImage* background = nullptr;
    if (mode == platform::GraphicsMode::Hd && shown.picture >= 0) {
        background = background_(shown.picture, shown.mirrored, shown.ega_palette, scale);
    }
    const bool picture_changed =
        shown.picture != last_picture_ || shown.mirrored != last_mirrored_ || shown.ega_palette != last_ega_palette_;
    const bool full = scale != scale_ || mode != mode_ || last_visual_.size() != pixels ||
                      last_cover_.size() != pixels || (mode == platform::GraphicsMode::Hd && picture_changed);
    if (scale != scale_) {
        scale_ = scale;
        frame_.assign(pixels * static_cast<std::size_t>(scale) * static_cast<std::size_t>(scale) * 4, 0);
    }
    mode_ = mode;
    int left = screen_width;
    int top = screen_height;
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < screen_height; ++y) {
        for (int x = 0; x < screen_width; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * screen_width) + static_cast<std::size_t>(x);
            if (!full && shown.visual[at] == last_visual_[at] && shown.cover[at] == last_cover_[at]) {
                continue;
            }
            draw_pixel(shown, background, x, y);
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }
    if (right >= 0) {
        dirty_.push_back(Rect{
            .x = left * scale_,
            .y = top * scale_,
            .width = (right - left + 1) * scale_,
            .height = (bottom - top + 1) * scale_,
        });
    }
    last_visual_.assign(shown.visual.begin(), shown.visual.end());
    last_cover_.assign(shown.cover.begin(), shown.cover.end());
    last_picture_ = shown.picture;
    last_mirrored_ = shown.mirrored;
    last_ega_palette_ = shown.ega_palette;
}

std::vector<Rect> GameScreen::take_dirty() {
    return std::exchange(dirty_, {});
}

} // namespace sci::app
