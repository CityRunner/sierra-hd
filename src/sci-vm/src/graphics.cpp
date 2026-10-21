// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sci.cpp (initGraphics), engine/state.cpp (strSplitLanguage)
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

#include "graphics.hpp"

#include "machine.hpp"

namespace sci::vm {

namespace {

constexpr int language_none = 0;
constexpr int language_english = 1;

int language_of(char c) {
    switch (c) {
    case 'F':
        return 33;
    case 'S':
        return 34;
    case 'I':
        return 39;
    case 'G':
        return 49;
    case 'J':
    case 'j':
        return 81;
    case 'P':
        return 351;
    default:
        return language_none;
    }
}

// SciEngine::getSciLanguageString for single-byte text.
std::string language_string(std::string_view text, int requested, int* found_language) {
    int found = language_none;
    std::size_t position = 0;
    for (; position < text.size(); ++position) {
        const char c = text[position];
        if (c == '%' || c == '#') {
            found = language_of(position + 1 < text.size() ? text[position + 1] : '\0');
            if (found != language_none) {
                break;
            }
        }
    }
    if (found_language != nullptr) {
        *found_language = found;
    }
    if (found == requested && found != language_none) {
        return std::string(text.substr(position + 2));
    }
    return std::string(text.substr(0, position));
}

} // namespace

Graphics::Graphics() = default;
Graphics::~Graphics() = default;

Faults& Graphics::faults() {
    return machine_->faults();
}

void Graphics::attach(Machine& machine) {
    machine_ = &machine;
    screen_ = std::make_unique<Screen>();
    cache_ = std::make_unique<GfxCache>(machine);
    ports_ = std::make_unique<Ports>(*this);
    paint_ = std::make_unique<Paint>(*this);
    animate_ = std::make_unique<Animate>(*this);
    text_ = std::make_unique<Text>(*this);
    transitions_ = std::make_unique<Transitions>(*this);
    compare_ = std::make_unique<Compare>(*this);
    cursor_ = std::make_unique<Cursor>(*this);
    menu_ = std::make_unique<Menu>(*this);
    controls_ = std::make_unique<Controls>(*this);
    ports_->init();
}

std::vector<Reg> Graphics::engine_hunks() const {
    return ports_ ? ports_->engine_hunks() : std::vector<Reg>{};
}

std::string Graphics::language_text(std::string_view text, int language) {
    return language_string(text, language, nullptr);
}

std::string Graphics::str_split(std::string_view text, const char* separator) {
    const SelectorCache& s = machine_->selector();
    int active = language_english;
    if (s.print_lang != no_selector) {
        active = machine_->read_selector_value(machine_->game_object(), s.print_lang);
        if (active == language_none) {
            active = language_english;
        }
    }
    int subtitle = language_none;
    if (s.subtitle_lang != no_selector) {
        subtitle = machine_->read_selector_value(machine_->game_object(), s.subtitle_lang);
    }
    int found = language_none;
    std::string result = language_string(text, active, &found);
    if (separator == nullptr || subtitle == language_none || found == language_none) {
        return result;
    }
    if (subtitle == language_english || subtitle == found) {
        result += separator;
        result += language_string(text, subtitle, nullptr);
    }
    return result;
}

} // namespace sci::vm
