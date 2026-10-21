// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 graphics at 1x, as ScummVM's GfxScreen, GfxPorts, GfxPaint16,
// GfxText16, GfxCompare, GfxAnimate, GfxCursor, GfxMenu and GfxTransitions
// (engines/sci/graphics, ported in the gfx_* files). They hold pointers to
// each other in ScummVM; here each reaches the others through this object.

#pragma once

#include "gfx_animate.hpp"
#include "gfx_cache.hpp"
#include "gfx_compare.hpp"
#include "gfx_cursor.hpp"
#include "gfx_menu.hpp"
#include "gfx_paint.hpp"
#include "gfx_ports.hpp"
#include "gfx_screen.hpp"
#include "gfx_text.hpp"
#include "sci/vm/reg.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sci::vm {

class Faults;
class Machine;

class Graphics {
public:
    Graphics();
    ~Graphics();
    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;
    Graphics(Graphics&&) = delete;
    Graphics& operator=(Graphics&&) = delete;

    // SciEngine::initGraphics.
    void attach(Machine& machine);

    // On a restart (SciEngine::runGame): the menus go.
    void on_restart_game() { menu().reset(); }

    [[nodiscard]] bool attached() const { return machine_ != nullptr; }

    [[nodiscard]] Machine& machine() { return *machine_; }

    [[nodiscard]] Faults& faults();

    [[nodiscard]] Screen& screen() { return *screen_; }

    [[nodiscard]] GfxCache& cache() { return *cache_; }

    [[nodiscard]] Ports& ports() { return *ports_; }

    [[nodiscard]] Paint& paint() { return *paint_; }

    [[nodiscard]] Text& text() { return *text_; }

    [[nodiscard]] Compare& compare() { return *compare_; }

    [[nodiscard]] Animate& animate() { return *animate_; }

    [[nodiscard]] Cursor& cursor() { return *cursor_; }

    [[nodiscard]] Menu& menu() { return *menu_; }

    [[nodiscard]] Transitions& transitions() { return *transitions_; }

    [[nodiscard]] std::vector<Reg> engine_hunks() const;

    // SciEngine::strSplit: the text up to a marker of another language
    // (%F, #G and so on), with the subtitle language after `separator`.
    [[nodiscard]] std::string str_split(std::string_view text, const char* separator);
    // SciEngine::getSciLanguageString: the part of `text` in `language`.
    [[nodiscard]] static std::string language_text(std::string_view text, int language);

private:
    Machine* machine_ = nullptr;
    std::unique_ptr<Screen> screen_;
    std::unique_ptr<GfxCache> cache_;
    std::unique_ptr<Ports> ports_;
    std::unique_ptr<Paint> paint_;
    std::unique_ptr<Text> text_;
    std::unique_ptr<Compare> compare_;
    std::unique_ptr<Animate> animate_;
    std::unique_ptr<Cursor> cursor_;
    std::unique_ptr<Menu> menu_;
    std::unique_ptr<Transitions> transitions_;
};

} // namespace sci::vm
