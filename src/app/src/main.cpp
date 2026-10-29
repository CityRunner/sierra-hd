// SPDX-License-Identifier: GPL-3.0-or-later
//
// sierra-hd: the game in a window (milestone 4b), and the room viewer of
// release v0.1 (milestone 3a) with --viewer.
//
//   sierra-hd [--viewer] [game folder]
//   sierra-hd [--replay <file>] [--seed <n>] [--fast] [--end-state <file>] [game folder]
//   sierra-hd --version | --help
//
// The configuration file sierra-hd.ini and the hint files (hints/) are
// next to the executable (spec section 10.2). The music plays through SDL3
// audio (milestone 7, audio_output.hpp).

#include "audio_output.hpp"
#include "file_store.hpp"
#include "game_screen.hpp"
#include "sci/audio/adlib.hpp"
#include "sci/audio/mt32_driver.hpp"
#include "sci/core/assert.hpp"
#include "sci/core/error.hpp"
#include "sci/core/fp_profile.hpp"
#include "sci/core/log.hpp"
#include "sci/core/sha256.hpp"
#include "sci/platform/analysis_cache.hpp"
#include "sci/platform/config.hpp"
#include "sci/platform/game_files.hpp"
#include "sci/platform/game_frame.hpp"
#include "sci/platform/hd_pictures.hpp"
#include "sci/platform/identify.hpp"
#include "sci/platform/mt32_roms.hpp"
#include "sci/platform/save_folder.hpp"
#include "sci/platform/thread_executor.hpp"
#include "sci/view/cursor.hpp"
#include "sci/vm/events.hpp"
#include "sci/vm/game.hpp"
#include "sci/vm/reg.hpp"
#include "sci/vm/state_hash.hpp"
#include "viewer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// SDL's window coordinates (single precision). They only place the pointer;
// nothing that is drawn depends on them, and arithmetic on them is in double.
using SdlCoordinate = decltype(SDL_FPoint::x);

namespace fs = std::filesystem;
using sci::app::Command;

constexpr std::string_view help_text = "usage: sierra-hd [--viewer] [game folder]\n"
                                       "       sierra-hd [--replay <file>] [--seed <n>] [--fast] [--end-state <file>]\n"
                                       "                 [game folder]\n"
                                       "       sierra-hd --version | --help\n"
                                       "\n"
                                       "--replay plays the input of a replay file\n"
                                       "(docs/formats/replay-and-trace.md) into the game, with the\n"
                                       "random seed of --seed (default 0); keyboard and mouse still work.\n"
                                       "--fast runs the game as fast as it can instead of in real time\n"
                                       "(the game plays the same; the screen is shown about 60 times a\n"
                                       "second of real time and most of the music is dropped).\n"
                                       "--end-state writes the game's tick, globals and state hash to\n"
                                       "the file when it ends (as sci-vm-trace --globals and\n"
                                       "--state-hash write them at a tick past the end).\n"
                                       "\n"
                                       "In the game:\n"
                                       "  F12               HD or original mode\n"
                                       "  F11, Alt+Enter    fullscreen\n"
                                       "\n"
                                       "In the room viewer (--viewer):\n"
                                       "  Left, Right       previous or next picture, view or font\n"
                                       "  Up, Down          views: previous or next loop\n"
                                       "  Tab               pictures, views, fonts\n"
                                       "  H                 HD or original mode\n"
                                       "  P                 pictures: visual or priority map\n"
                                       "  + and -           scale N up or down; 0: the largest N that fits\n"
                                       "  Space             views: stop or start the animation\n"
                                       "  F11, Alt+Enter    fullscreen\n"
                                       "  Escape            quit\n";

void print_log(sci::core::LogLevel level, sci::core::LogCategory category, std::string_view message) noexcept {
    std::println(stderr, "[{}:{}] {}", sci::core::to_string(category), sci::core::to_string(level), message);
}

void print_assertion(std::string_view expression, const std::source_location& location) noexcept {
    std::println(stderr, "assertion failed: {} ({}:{})", expression, location.file_name(), location.line());
}

fs::path base_path() {
    const char* base = SDL_GetBasePath();
    return base != nullptr ? fs::path(std::u8string(reinterpret_cast<const char8_t*>(base))) // NOLINT
                           : fs::current_path();
}

void show_error(SDL_Window* window, const std::string& message) {
    std::println(stderr, "{}", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "sierra-hd", message.c_str(), window);
}

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Asks for the game folder with the system's folder dialog; nothing if the
// user cancels or the system has no dialog.
std::optional<fs::path> choose_folder(SDL_Window* window) {
    struct Choice {
        bool done = false;
        std::optional<fs::path> folder;
    } choice;

    SDL_ShowOpenFolderDialog(
        [](void* userdata, const char* const* list, int /*filter*/) {
            auto* result = static_cast<Choice*>(userdata);
            if (list != nullptr && *list != nullptr) {
                result->folder = fs::path(std::u8string(reinterpret_cast<const char8_t*>(*list))); // NOLINT
            }
            result->done = true;
        },
        &choice, window, nullptr, false);
    while (!choice.done) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 50) && event.type == SDL_EVENT_QUIT) {
            break;
        }
    }
    return choice.folder;
}

std::optional<Command> command_for(const SDL_KeyboardEvent& key) {
    switch (key.key) {
    case SDLK_RIGHT:
        return Command::Next;
    case SDLK_LEFT:
        return Command::Previous;
    case SDLK_DOWN:
        return Command::NextLoop;
    case SDLK_UP:
        return Command::PreviousLoop;
    case SDLK_TAB:
        return Command::NextBrowser;
    case SDLK_H:
        return Command::ToggleMode;
    case SDLK_P:
        return Command::TogglePriority;
    case SDLK_PLUS:
    case SDLK_EQUALS:
    case SDLK_KP_PLUS:
        return Command::ScaleUp;
    case SDLK_MINUS:
    case SDLK_KP_MINUS:
        return Command::ScaleDown;
    case SDLK_0:
    case SDLK_KP_0:
        return Command::ScaleAuto;
    case SDLK_SPACE:
        return Command::ToggleAnimation;
    default:
        return std::nullopt;
    }
}

struct SdlDeleter {
    void operator()(SDL_Window* window) const { SDL_DestroyWindow(window); }

    void operator()(SDL_Renderer* renderer) const { SDL_DestroyRenderer(renderer); }

    void operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }

    void operator()(SDL_Cursor* cursor) const { SDL_DestroyCursor(cursor); }
};

// Presents the viewer's frame: uploads the changed rectangles to the
// texture and draws it centred, letterboxed, at 1:1 in physical pixels;
// and the game's cursor over it where the program draws the cursor.
class Presenter {
public:
    explicit Presenter(SDL_Renderer* renderer) : renderer_(renderer) {}

    void present(sci::app::Viewer& viewer) {
        const std::vector<sci::app::Rect> dirty = viewer.take_dirty();
        present(viewer.frame(), viewer.scale(), dirty);
    }

    // The frame at 320N x 200N; `dirty` are the rectangles changed since the
    // last frame.
    void present(const std::vector<std::uint8_t>& frame, int scale, const std::vector<sci::app::Rect>& dirty) {
        const int width = sci::app::screen_width * scale;
        const int height = sci::app::screen_height * scale;
        if (!texture_ || width != width_ || height != height_) {
            texture_.reset(
                SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height));
            SDL_SetTextureScaleMode(texture_.get(), SDL_SCALEMODE_NEAREST);
            width_ = width;
            height_ = height;
            const SDL_Rect all{.x = 0, .y = 0, .w = width, .h = height};
            SDL_UpdateTexture(texture_.get(), &all, frame.data(), width * 4);
        } else {
            for (const sci::app::Rect& r : dirty) {
                const SDL_Rect rect{.x = r.x, .y = r.y, .w = r.width, .h = r.height};
                const std::size_t offset = ((static_cast<std::size_t>(r.y) * static_cast<std::size_t>(width)) +
                                            static_cast<std::size_t>(r.x)) *
                                           4;
                SDL_UpdateTexture(texture_.get(), &rect, std::span(frame).subspan(offset).data(), width * 4);
            }
        }
        redraw();
    }

    // Draws the frame (and the cursor) again, without uploading anything.
    void redraw() {
        if (!texture_) {
            return;
        }
        const int width = width_;
        const int height = height_;
        int output_width = 0;
        int output_height = 0;
        SDL_GetCurrentRenderOutputSize(renderer_, &output_width, &output_height);
        SDL_SetRenderViewport(renderer_, nullptr);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        // The frame at 1:1 in the middle; the rest is letterbox. An integer
        // viewport keeps the placement exact.
        const SDL_Rect target{
            .x = (output_width - width) / 2,
            .y = (output_height - height) / 2,
            .w = width,
            .h = height,
        };
        SDL_SetRenderViewport(renderer_, &target);
        SDL_RenderTexture(renderer_, texture_.get(), nullptr, nullptr);
        if (cursor_) {
            // The cursor, already N x N, at 1:1 with its hotspot on the
            // mouse's physical pixel.
            SDL_SetRenderViewport(renderer_, nullptr);
            const SDL_Rect at{
                .x = cursor_x_ - cursor_hotspot_x_,
                .y = cursor_y_ - cursor_hotspot_y_,
                .w = cursor_size_,
                .h = cursor_size_,
            };
            SDL_FRect at_f{};
            SDL_RectToFRect(&at, &at_f);
            SDL_RenderTexture(renderer_, cursor_.get(), nullptr, &at_f);
        }
        SDL_RenderPresent(renderer_);
        offset_x_ = target.x;
        offset_y_ = target.y;
    }

    // The cursor to draw over the frame (sci::app::CursorMode::Drawn), or
    // none.
    void set_cursor(const sci::app::CursorImage* image) {
        cursor_.reset();
        if (image == nullptr || image->size == 0) {
            return;
        }
        cursor_.reset(
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image->size, image->size));
        if (!cursor_) {
            return;
        }
        SDL_SetTextureBlendMode(cursor_.get(), SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(cursor_.get(), SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(cursor_.get(), nullptr, image->pixels.data(), image->size * 4);
        cursor_size_ = image->size;
        cursor_hotspot_x_ = image->hotspot_x;
        cursor_hotspot_y_ = image->hotspot_y;
    }

    [[nodiscard]] bool draws_cursor() const { return static_cast<bool>(cursor_); }

    // Where the drawn cursor's hotspot is, in physical pixels of the output.
    void move_cursor(int x, int y) {
        cursor_x_ = x;
        cursor_y_ = y;
    }

    // Where the frame starts in the output, in physical pixels.
    [[nodiscard]] int offset_x() const { return offset_x_; }

    [[nodiscard]] int offset_y() const { return offset_y_; }

private:
    SDL_Renderer* renderer_;
    std::unique_ptr<SDL_Texture, SdlDeleter> texture_;
    std::unique_ptr<SDL_Texture, SdlDeleter> cursor_;
    int cursor_size_ = 0;
    int cursor_hotspot_x_ = 0;
    int cursor_hotspot_y_ = 0;
    int cursor_x_ = 0;
    int cursor_y_ = 0;
    int width_ = 0;
    int height_ = 0;
    int offset_x_ = 0;
    int offset_y_ = 0;
};

std::optional<sci::platform::Config> load_config(const fs::path& path) {
    const auto text = read_text(path);
    if (!text) {
        return sci::platform::Config{};
    }
    auto parsed = sci::platform::parse_config(*text);
    if (!parsed) {
        show_error(nullptr, std::format("{}: {}", path.string(), parsed.error().message));
        return std::nullopt;
    }
    for (const std::string& warning : parsed->warnings) {
        sci::core::log(sci::core::LogLevel::Warning, sci::core::LogCategory::General, warning);
    }
    return parsed->config;
}

struct Game {
    sci::resource::ResourceManager manager;
    sci::platform::Identification identification;
    std::vector<sci::resource::GameFile> files; // For the files the game reads.
};

// Opens and identifies the game in the configured folder, asking for the
// folder if none is configured. Reports problems to the user.
std::optional<Game> open_game(SDL_Window* window, sci::platform::Config& config, const fs::path& config_path) {
    if (config.game_folder.empty()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "sierra-hd", "Choose the folder of The Colonel's Bequest.",
                                 window);
        auto folder = choose_folder(window);
        if (!folder) {
            show_error(window, std::format("No game folder chosen. Start sierra-hd with the folder as its argument, "
                                           "or set game_folder in {}.",
                                           config_path.string()));
            return std::nullopt;
        }
        config.game_folder = *folder;
    }
    auto files = sci::platform::read_game_folder(config.game_folder);
    if (!files) {
        show_error(window, std::format("Cannot read {}: {}", config.game_folder.string(), files.error().message));
        return std::nullopt;
    }
    auto identification = sci::platform::identify_game(*files);
    if (!identification) {
        show_error(window, identification.error().message);
        return std::nullopt;
    }
    sci::core::log(sci::core::LogLevel::Info, sci::core::LogCategory::General, identification->name);
    for (const std::string& warning : identification->warnings) {
        sci::core::log(sci::core::LogLevel::Warning, sci::core::LogCategory::General, warning);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "sierra-hd", warning.c_str(), window);
    }
    std::vector<sci::resource::GameFile> copies = *files;
    auto manager = sci::resource::ResourceManager::open(std::move(*files));
    if (!manager) {
        show_error(window, std::format("Cannot open the game: {}", manager.error().message));
        return std::nullopt;
    }
    return Game{
        .manager = std::move(*manager),
        .identification = std::move(*identification),
        .files = std::move(copies),
    };
}

// The hint files next to the executable. They are made for the tested
// release; their PIC hashes keep them from being used with any other.
std::optional<fs::path> hint_directory(const fs::path& base) {
    fs::path hints = base / "hints";
    if (!fs::is_directory(hints)) {
        sci::core::log(sci::core::LogLevel::Warning, sci::core::LogCategory::General,
                       std::format("{} not found; pictures are analysed without hints", hints.string()));
        return std::nullopt;
    }
    return hints;
}

// The first launch's analysis of every picture (spec section 10.2), with a
// progress bar; later launches find the entries and skip it. False when the
// user closed the window meanwhile.
bool prepare_analysis(SDL_Renderer* renderer, sci::platform::AnalysisCache& analysis) {
    const auto all = analysis.all_variants();
    const auto missing = analysis.missing(all);
    if (missing.empty()) {
        return true;
    }
    const std::uint64_t start = SDL_GetTicks();
    bool open = true;
    const bool done = analysis.prepare(missing, [&](std::size_t finished, std::size_t total) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                open = false;
            }
        }
        int width = 0;
        int height = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
        SDL_SetRenderViewport(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        // Integer rectangles, converted for SDL's drawing calls.
        const SDL_Rect frame{.x = width / 5, .y = height / 2, .w = width * 3 / 5, .h = 12};
        SDL_Rect bar = frame;
        bar.w = total == 0 ? frame.w : static_cast<int>(static_cast<std::size_t>(frame.w) * finished / total);
        SDL_FRect frame_f{};
        SDL_FRect bar_f{};
        SDL_RectToFRect(&frame, &frame_f);
        SDL_RectToFRect(&bar, &bar_f);
        SDL_SetRenderDrawColor(renderer, 85, 85, 85, 255);
        SDL_RenderRect(renderer, &frame_f);
        SDL_SetRenderDrawColor(renderer, 170, 170, 170, 255);
        SDL_RenderFillRect(renderer, &bar_f);
        const std::string label = std::format("Analysing the pictures for HD: {} of {}", finished, total);
        const SDL_Rect label_at{.x = frame.x, .y = frame.y - 16, .w = 0, .h = 0};
        SDL_FRect label_f{};
        SDL_RectToFRect(&label_at, &label_f);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDebugText(renderer, label_f.x, label_f.y, label.c_str());
        SDL_RenderPresent(renderer);
        return open;
    });
    sci::core::log(
        sci::core::LogLevel::Info, sci::core::LogCategory::General,
        std::format("analysed {} scenes in {} ms{}", missing.size(), SDL_GetTicks() - start, done ? "" : " (stopped)"));
    return open;
}

// Handles one event; false when the program should end.
bool handle_event(const SDL_Event& event, SDL_Window* window, sci::app::Viewer& viewer, bool& changed) {
    if (event.type == SDL_EVENT_QUIT) {
        return false;
    }
    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_EXPOSED) {
        changed = true;
        return true;
    }
    if (event.type != SDL_EVENT_KEY_DOWN) {
        return true;
    }
    const SDL_KeyboardEvent& key = event.key;
    if (key.key == SDLK_ESCAPE) {
        return false;
    }
    if (key.key == SDLK_F11 || (key.key == SDLK_RETURN && (key.mod & SDL_KMOD_ALT) != 0)) {
        const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreen(window, !fullscreen);
    } else if (const auto command = command_for(key)) {
        viewer.handle(*command);
        changed = true;
    }
    return true;
}

void event_loop(SDL_Window* window, SDL_Renderer* renderer, sci::app::Viewer& viewer) {
    Presenter presenter(renderer);
    bool changed = true;
    std::uint64_t last = SDL_GetTicks();
    while (true) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 10)) {
            do {
                if (!handle_event(event, window, viewer, changed)) {
                    return;
                }
            } while (SDL_PollEvent(&event));
        }
        int width = 0;
        int height = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
        viewer.set_window_size(width, height);
        const std::uint64_t now = SDL_GetTicks();
        viewer.advance(static_cast<int>(std::min<std::uint64_t>(now - last, 1000)));
        last = now;
        // Present after a command, a resize or an animation step only.
        if (changed || viewer.has_changes()) {
            presenter.present(viewer);
            changed = false;
        }
    }
}

// The SDL key event as a neutral key press (getScummVMEvent's view of it).
sci::app::KeyPress key_press(const SDL_KeyboardEvent& key) {
    using Key = sci::app::KeyPress::Key;
    sci::app::KeyPress press;
    press.shift = (key.mod & SDL_KMOD_SHIFT) != 0;
    press.ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    press.alt = (key.mod & SDL_KMOD_ALT) != 0;
    press.num_lock = (key.mod & SDL_KMOD_NUM) != 0;

    struct Keypad {
        SDL_Keycode code;
        Key key;
        char character;
    };

    static constexpr std::array<Keypad, 16> keypad{
        {
            {.code = SDLK_KP_0, .key = Key::Insert, .character = '0'},
            {.code = SDLK_KP_1, .key = Key::End, .character = '1'},
            {.code = SDLK_KP_2, .key = Key::Down, .character = '2'},
            {.code = SDLK_KP_3, .key = Key::PageDown, .character = '3'},
            {.code = SDLK_KP_4, .key = Key::Left, .character = '4'},
            {.code = SDLK_KP_5, .key = Key::Center, .character = '5'},
            {.code = SDLK_KP_6, .key = Key::Right, .character = '6'},
            {.code = SDLK_KP_7, .key = Key::Home, .character = '7'},
            {.code = SDLK_KP_8, .key = Key::Up, .character = '8'},
            {.code = SDLK_KP_9, .key = Key::PageUp, .character = '9'},
            {.code = SDLK_KP_PERIOD, .key = Key::Delete, .character = '.'},
            {.code = SDLK_KP_ENTER, .key = Key::Enter, .character = 0},
            {.code = SDLK_KP_PLUS, .key = Key::Character, .character = '+'},
            {.code = SDLK_KP_MINUS, .key = Key::Character, .character = '-'},
            {.code = SDLK_KP_MULTIPLY, .key = Key::Character, .character = '*'},
            {.code = SDLK_KP_DIVIDE, .key = Key::Character, .character = '/'},
        },
    };
    for (const Keypad& k : keypad) {
        if (key.key == k.code) {
            press.key = k.key;
            press.character = k.character;
            press.keypad = true;
            return press;
        }
    }
    switch (key.key) {
    case SDLK_RETURN:
        press.key = Key::Enter;
        return press;
    case SDLK_ESCAPE:
        press.key = Key::Escape;
        return press;
    case SDLK_BACKSPACE:
        press.key = Key::Backspace;
        return press;
    case SDLK_TAB:
        press.key = Key::Tab;
        return press;
    case SDLK_UP:
        press.key = Key::Up;
        return press;
    case SDLK_DOWN:
        press.key = Key::Down;
        return press;
    case SDLK_LEFT:
        press.key = Key::Left;
        return press;
    case SDLK_RIGHT:
        press.key = Key::Right;
        return press;
    case SDLK_HOME:
        press.key = Key::Home;
        return press;
    case SDLK_END:
        press.key = Key::End;
        return press;
    case SDLK_PAGEUP:
        press.key = Key::PageUp;
        return press;
    case SDLK_PAGEDOWN:
        press.key = Key::PageDown;
        return press;
    case SDLK_INSERT:
        press.key = Key::Insert;
        return press;
    case SDLK_DELETE:
        press.key = Key::Delete;
        return press;
    default:
        break;
    }
    if (key.key >= SDLK_F1 && key.key <= SDLK_F10) {
        press.key = Key::Function;
        press.function = static_cast<int>(key.key - SDLK_F1) + 1;
        return press;
    }
    // The character with Shift (and Caps Lock) applied.
    const SDL_Keycode shifted = SDL_GetKeyFromScancode(key.scancode, key.mod, false);
    if (shifted >= 0x20 && shifted < 0x7F) {
        press.key = Key::Character;
        press.character = static_cast<char>(shifted);
    }
    return press;
}

// Runs the game (spec sections 7 and 10.1): the interpreter runs on this,
// the main thread, and while it waits, the host shows its screen and passes
// the input on, until the wait's real time has passed.
class GameHost final : public sci::vm::GameHost {
public:
    // How long the frame shown is kept while the HD images of a new picture
    // are made: the room-entry budget (docs/ASSUMPTIONS.md, "Milestone 3").
    static constexpr std::uint64_t hd_hold_milliseconds = 1000;

    GameHost(SDL_Window* window, SDL_Renderer* renderer, const sci::resource::ResourceManager& manager,
             sci::platform::HdPictures& pictures, sci::platform::Config& config)
        : window_(window), renderer_(renderer), manager_(&manager), presenter_(renderer), config_(&config),
          screen_([&pictures](std::span<const sci::vm::ShownPicture> stack, std::span<const sci::vm::ShownLine> lines,
                              int scale) { return pictures.request(stack, lines, scale); }),
          start_(SDL_GetTicks()) {
        // A black frame: the first presentation sets the renderer up (tens
        // of milliseconds, over 100 in a debug build), here before the
        // music starts rather than as a stall in it.
        const int scale = scale_for_window();
        const std::vector<std::uint8_t> black(static_cast<std::size_t>(sci::app::screen_width) *
                                                  sci::app::screen_height * static_cast<std::size_t>(scale) *
                                                  static_cast<std::size_t>(scale) * 4,
                                              0);
        presenter_.present(black, scale, {});
    }

    [[nodiscard]] sci::vm::LocalTime local_time() override {
        const auto now = std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::system_clock::now());
        const auto local = now.get_local_time();
        const auto day = std::chrono::floor<std::chrono::days>(local);
        const std::chrono::year_month_day date(day);
        const std::chrono::hh_mm_ss time(std::chrono::floor<std::chrono::seconds>(local - day));
        return sci::vm::LocalTime{
            .year = static_cast<int>(date.year()) - 1900,
            .month = static_cast<int>(static_cast<unsigned>(date.month())) - 1,
            .day = static_cast<int>(static_cast<unsigned>(date.day())),
            .hour = static_cast<int>(time.hours().count()),
            .minute = static_cast<int>(time.minutes().count()),
            .second = static_cast<int>(time.seconds().count()),
        };
    }

    void wait(sci::vm::Game& game, std::uint32_t milliseconds) override {
        if (fast_) {
            wait_fast(game, milliseconds);
            return;
        }
        const std::uint64_t wait_start = virtual_;
        virtual_ += milliseconds;
        while (true) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                handle(game, event);
            }
            present(game);
            const std::uint64_t now = SDL_GetTicks() - start_;
            // The music of the time passed so far in this wait.
            game.pass_wait(static_cast<std::uint32_t>(
                std::min<std::uint64_t>(now > wait_start ? now - wait_start : 0, milliseconds)));
            if (now >= virtual_) {
                // Far behind (a slow machine, a dialog): catch up, no burst.
                if (now > virtual_ + 250) {
                    virtual_ = now;
                }
                return;
            }
            // In time: all the music due is made, so an underrun since the
            // last wait can be made up for (AudioOutput::recover).
            if (audio_ != nullptr) {
                audio_->recover();
            }
            SDL_Delay(static_cast<std::uint32_t>(std::min<std::uint64_t>(virtual_ - now, 4)));
        }
    }

    // The music's output, told when the host is in time (nullptr: none).
    void set_audio(sci::app::AudioOutput* audio) { audio_ = audio; }

    // --fast: the waits do not wait for real time (milestone 9, the
    // playthrough in the program; docs/playthrough.md).
    void set_fast(bool fast) { fast_ = fast; }

private:
    // How often the fast waits present the screen, in real milliseconds.
    static constexpr std::uint64_t fast_frame_milliseconds = 16;

    // A wait that does not wait: the input is passed on, the music of the
    // whole wait is made at once (the queue drops what does not fit,
    // AudioOutput), and the screen is presented at most once per
    // fast_frame_milliseconds. Only the pacing differs from wait: the game
    // sees the same virtual time and the same music timer calls.
    void wait_fast(sci::vm::Game& game, std::uint32_t milliseconds) {
        virtual_ += milliseconds;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handle(game, event);
        }
        const std::uint64_t now = SDL_GetTicks();
        if (redraw_ || now - last_fast_present_ >= fast_frame_milliseconds) {
            present(game);
            last_fast_present_ = now;
        }
        game.pass_wait(milliseconds);
    }

    [[nodiscard]] int scale_for_window() const {
        int width = 0;
        int height = 0;
        SDL_GetCurrentRenderOutputSize(renderer_, &width, &height);
        const int fit = std::max(1, std::min(width / sci::app::screen_width, height / sci::app::screen_height));
        return config_->scale > 0 ? std::min(config_->scale, fit) : std::min(fit, sci::platform::max_scale);
    }

    void handle(sci::vm::Game& game, const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            game.quit();
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
            redraw_ = true;
            break;
        case SDL_EVENT_KEY_DOWN: {
            const SDL_KeyboardEvent& key = event.key;
            if (key.key == SDLK_F11 || (key.key == SDLK_RETURN && (key.mod & SDL_KMOD_ALT) != 0)) {
                const bool fullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
                SDL_SetWindowFullscreen(window_, !fullscreen);
            } else if (key.key == SDLK_F12) {
                config_->mode = config_->mode == sci::platform::GraphicsMode::Hd ? sci::platform::GraphicsMode::Original
                                                                                 : sci::platform::GraphicsMode::Hd;
                redraw_ = true;
            } else if (const auto sci_event = sci::app::sci_key_event(key_press(key))) {
                game.input(*sci_event);
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            last_mouse_ = sci::app::window_to_1x(static_cast<double>(event.motion.x),
                                                 static_cast<double>(event.motion.y), geometry());
            game.move_mouse(last_mouse_);
            pointer_moved_ = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            last_mouse_ = sci::app::window_to_1x(static_cast<double>(event.button.x),
                                                 static_cast<double>(event.button.y), geometry());
            std::uint16_t modifiers = sci::vm::modifier_none;
            const SDL_Keymod keys = SDL_GetModState();
            if ((keys & SDL_KMOD_ALT) != 0) {
                modifiers |= sci::vm::modifier_alt;
            }
            if ((keys & SDL_KMOD_CTRL) != 0) {
                modifiers |= sci::vm::modifier_ctrl;
            }
            if ((keys & SDL_KMOD_SHIFT) != 0) {
                modifiers |= sci::vm::modifier_shift;
            }
            // The mouse driver marked the right button as Shift, the middle as Ctrl.
            if (event.button.button == SDL_BUTTON_RIGHT) {
                modifiers |= sci::vm::modifier_shift;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                modifiers |= sci::vm::modifier_ctrl;
            }
            game.input(sci::vm::SciEvent{
                .type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? sci::vm::event_mouse_press
                                                                  : sci::vm::event_mouse_release,
                .modifiers = modifiers,
                .character = 0,
                .mouse = last_mouse_,
            });
            break;
        }
        default:
            break;
        }
    }

    // Where the frame is in the window.
    [[nodiscard]] sci::app::FrameGeometry geometry() const {
        return sci::app::FrameGeometry{
            .scale = std::max(screen_.scale(), 1),
            .offset_x = presenter_.offset_x(),
            .offset_y = presenter_.offset_y(),
            .pixel_density = static_cast<double>(SDL_GetWindowPixelDensity(window_)),
        };
    }

    void present(sci::vm::Game& game) {
        const sci::vm::ShownScreen shown = game.screen();
        const int scale = scale_for_window();
        if (!redraw_ && shown.serial == last_serial_ && scale == screen_.scale() && !screen_.hd_pending()) {
            update_cursor(shown, scale);
            // A drawn cursor that moved is drawn again.
            if (presenter_.draws_cursor() && std::exchange(pointer_moved_, false)) {
                presenter_.redraw();
            }
            return;
        }
        // A picture whose HD images are being made: the frame shown stays
        // for up to hd_hold_milliseconds, then the picture is shown at 1x
        // until they come (HdPictures::request).
        const std::uint64_t ticks = SDL_GetTicks();
        if (!screen_.waiting()) {
            hold_start_ = ticks;
        }
        screen_.update(shown, config_->mode, scale, config_->priority_snap, ticks - hold_start_ < hd_hold_milliseconds);
        if (screen_.waiting()) {
            return;
        }
        std::vector<sci::app::Rect> dirty;
        for (const sci::render::PixelRect& rect : screen_.take_dirty()) {
            dirty.push_back(sci::app::Rect{.x = rect.x, .y = rect.y, .width = rect.width, .height = rect.height});
        }
        presenter_.present(screen_.frame(), screen_.scale(), redraw_ ? full_frame() : dirty);
        redraw_ = false;
        last_serial_ = shown.serial;
        update_cursor(shown, scale);
        if (presenter_.draws_cursor() && std::exchange(pointer_moved_, false)) {
            presenter_.redraw();
        }
    }

    [[nodiscard]] std::vector<sci::app::Rect> full_frame() const {
        return {
            sci::app::Rect{
                .x = 0,
                .y = 0,
                .width = sci::app::screen_width * screen_.scale(),
                .height = sci::app::screen_height * screen_.scale(),
            },
        };
    }

    // The game's cursor scaled N x N (spec section 6.9): the window's cursor
    // where the system shows it at that size, else drawn over the frame
    // (sci::app::cursor_mode); and where the game put the mouse, the
    // pointer goes too.
    void update_cursor(const sci::vm::ShownScreen& shown, int scale) {
        const sci::app::FrameGeometry where = geometry();
        if (shown.mouse != last_mouse_) {
            const sci::app::WindowPoint point = sci::app::window_from_1x(shown.mouse, where);
            SDL_WarpMouseInWindow(window_, static_cast<SdlCoordinate>(point.x), static_cast<SdlCoordinate>(point.y));
            last_mouse_ = shown.mouse;
            pointer_moved_ = true;
        }
        if (presenter_.draws_cursor()) {
            SdlCoordinate x = 0;
            SdlCoordinate y = 0;
            SDL_GetMouseState(&x, &y);
            presenter_.move_cursor(static_cast<int>(std::floor(static_cast<double>(x) * where.pixel_density)),
                                   static_cast<int>(std::floor(static_cast<double>(y) * where.pixel_density)));
        }
        if (shown.cursor == cursor_shape_ && scale == cursor_scale_ && where.pixel_density == cursor_density_) {
            return;
        }
        cursor_shape_ = shown.cursor;
        cursor_scale_ = scale;
        cursor_density_ = where.pixel_density;
        cursor_.reset();
        presenter_.set_cursor(nullptr);
        pointer_moved_ = true;
        std::optional<sci::view::Cursor> decoded;
        if (shown.cursor >= 0) {
            const auto resource = manager_->load(sci::resource::ResourceId{
                .type = sci::resource::ResourceType::Cursor,
                .number = static_cast<std::uint16_t>(shown.cursor),
            });
            if (resource) {
                if (auto cursor = sci::view::decode_cursor(resource->data)) {
                    decoded = *cursor;
                }
            }
        }
        if (!decoded) {
            SDL_HideCursor();
            return;
        }
        const sci::app::CursorImage image = sci::app::scale_cursor(*decoded, scale);
        if (sci::app::cursor_mode(where.pixel_density, image.size) == sci::app::CursorMode::System) {
            SDL_Surface* surface = SDL_CreateSurfaceFrom(image.size, image.size, SDL_PIXELFORMAT_RGBA32,
                                                         const_cast<std::uint8_t*>(image.pixels.data()), // NOLINT
                                                         image.size * 4);
            if (surface != nullptr) {
                cursor_.reset(SDL_CreateColorCursor(surface, image.hotspot_x, image.hotspot_y));
                SDL_DestroySurface(surface);
            }
            if (cursor_) {
                SDL_SetCursor(cursor_.get());
                SDL_ShowCursor();
                return;
            }
        }
        // Drawn over the frame: also where the system cannot make the cursor.
        SDL_HideCursor();
        presenter_.set_cursor(&image);
    }

    SDL_Window* window_;
    SDL_Renderer* renderer_;
    const sci::resource::ResourceManager* manager_;
    Presenter presenter_;
    sci::platform::Config* config_;
    sci::platform::GameFrame screen_;
    std::uint64_t start_;
    std::uint64_t virtual_ = 0;
    bool fast_ = false;
    std::uint64_t last_fast_present_ = 0;
    std::uint64_t hold_start_ = 0;
    sci::app::AudioOutput* audio_ = nullptr;
    std::uint64_t last_serial_ = ~std::uint64_t{0};
    bool redraw_ = true;
    sci::vm::Point last_mouse_{.x = 160, .y = 100};
    bool pointer_moved_ = true;
    int cursor_shape_ = -2;
    int cursor_scale_ = 0;
    double cursor_density_ = 0;
    std::unique_ptr<SDL_Cursor, SdlDeleter> cursor_;
};

struct Options {
    bool viewer = false;
    std::optional<fs::path> game_folder;
    std::optional<fs::path> replay;
    std::optional<std::uint32_t> seed;
    bool fast = false;
    std::optional<fs::path> end_state;
};

// The command line, or nothing if it is not valid.
std::optional<Options> parse_options(std::span<char*> args) {
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const bool has_value = i + 1 < args.size();
        if (arg == "--viewer") {
            options.viewer = true;
        } else if (arg == "--replay" && has_value) {
            options.replay = fs::path(args[++i]);
        } else if (arg == "--fast") {
            options.fast = true;
        } else if (arg == "--end-state" && has_value) {
            options.end_state = fs::path(args[++i]);
        } else if (arg == "--seed" && has_value) {
            const std::string_view value = args[++i];
            std::uint32_t seed = 0;
            const auto [end, error] =
                std::from_chars(std::to_address(value.begin()), std::to_address(value.end()), seed);
            if (error != std::errc{} || end != std::to_address(value.end())) {
                return std::nullopt;
            }
            options.seed = seed;
        } else if (!arg.starts_with('-') && !options.game_folder) {
            options.game_folder = fs::path(arg);
        } else {
            return std::nullopt;
        }
    }
    if (options.viewer && (options.replay || options.seed || options.fast || options.end_state)) {
        return std::nullopt;
    }
    return options;
}

// The music device of the configuration (milestone 7): the MT-32 driver on
// the user's ROMs with the game's patch.001, or the AdLib driver with the
// game's patches (also when the MT-32 cannot be opened), through SDL3
// audio; or none (the silent driver) when the music is off, the patches
// cannot be read or there is no audio device.
std::unique_ptr<sci::app::AudioOutput> open_audio(const sci::resource::ResourceManager& manager,
                                                  const sci::platform::Config& config) {
    using sci::platform::MusicDevice;
    sci::platform::MusicChoice choice = sci::platform::choose_music_device(config);
    if (!choice.log.empty()) {
        sci::core::log(choice.warning ? sci::core::LogLevel::Warning : sci::core::LogLevel::Info,
                       sci::core::LogCategory::General, choice.log);
    }
    if (choice.device == MusicDevice::Off) {
        return nullptr;
    }
    if (choice.device == MusicDevice::Mt32) {
        auto patch = sci::audio::load_mt32_patch(manager);
        if (patch) {
            const double volume = static_cast<double>(config.volume) / sci::platform::max_volume;
            return sci::app::AudioOutput::open(std::make_unique<sci::audio::Mt32Driver>(std::move(choice.mt32), *patch),
                                               volume);
        }
        sci::core::log(
            sci::core::LogLevel::Warning, sci::core::LogCategory::General,
            std::format("music: the MT-32 patch cannot be read ({}); playing on the AdLib", patch.error().message));
    }
    auto patches = sci::audio::load_adlib_patches(manager);
    if (!patches) {
        sci::core::log(
            sci::core::LogLevel::Warning, sci::core::LogCategory::General,
            std::format("music: no AdLib patches ({}); the music plays without sound", patches.error().message));
        return nullptr;
    }
    return sci::app::AudioOutput::open(std::move(*patches),
                                       static_cast<double>(config.volume) / sci::platform::max_volume);
}

// --end-state: the game as it ended, in the lines of sci-vm-trace's
// --globals and --state-hash files for a tick past the end (requested tick
// 0 here): "<requested> <tick> <global>..." then the hashes, so the two can
// be compared after the first field.
bool write_end_state(const fs::path& path, sci::vm::Game& game) {
    std::ofstream file(path);
    const std::uint32_t ticks = game.ticks();
    file << std::format("0 {}", ticks);
    for (const sci::vm::Reg& value : game.globals()) {
        if (value.is_number()) {
            file << std::format(" {}", value.offset);
        } else {
            file << std::format(" {:04x}:{:04x}", value.segment, value.offset);
        }
    }
    file << '\n';
    const sci::vm::StateHash hash = game.state_hash();
    file << std::format("0 {} total {}\n", ticks, sci::core::to_hex(hash.total));
    file << std::format("0 {} persistent {}\n", ticks, sci::core::to_hex(hash.persistent));
    for (const sci::vm::StateComponentHash& component : hash.components) {
        file << std::format("0 {} {} {} {}\n", ticks, component.name, component.bytes,
                            sci::core::to_hex(component.digest));
    }
    file.close();
    return !file.fail();
}

// The input events of a replay file; an error message if it cannot be read.
sci::core::Result<std::vector<sci::vm::InputEvent>> read_replay(const fs::path& path) {
    const std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    if (!file) {
        return sci::core::make_error(sci::core::ErrorCode::NotFound, std::format("Cannot read {}", path.string()));
    }
    return sci::vm::parse_replay(text.str());
}

int run(const Options& options) {
    const fs::path base = base_path();
    const fs::path config_path = base / "sierra-hd.ini";
    auto config = load_config(config_path);
    if (!config) {
        return 1;
    }
    if (options.game_folder) {
        config->game_folder = *options.game_folder;
    }
    const bool viewer_mode = options.viewer;
    std::vector<sci::vm::InputEvent> replay;
    if (options.replay) {
        auto events = read_replay(*options.replay);
        if (!events) {
            show_error(nullptr, std::format("{}: {}", options.replay->string(), events.error().message));
            return 1;
        }
        replay = std::move(*events);
    }
    const std::unique_ptr<SDL_Window, SdlDeleter> window(
        SDL_CreateWindow("sierra-hd", sci::app::screen_width * 3, sci::app::screen_height * 3,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY));
    if (!window) {
        show_error(nullptr, std::format("Cannot open a window: {}", SDL_GetError()));
        return 1;
    }
    auto game = open_game(window.get(), *config, config_path);
    if (!game) {
        return 1;
    }
    const std::unique_ptr<SDL_Renderer, SdlDeleter> renderer(SDL_CreateRenderer(window.get(), nullptr));
    if (!renderer) {
        show_error(window.get(), std::format("Cannot draw into the window: {}", SDL_GetError()));
        return 1;
    }
    SDL_SetRenderVSync(renderer.get(), 1);
    SDL_SetWindowTitle(window.get(), std::format("sierra-hd {} - {}", SCI_VERSION, game->identification.name).c_str());
    sci::platform::ThreadExecutor executor;
    sci::platform::AnalysisCache analysis(game->manager, hint_directory(base), base / "cache", executor);
    if (!prepare_analysis(renderer.get(), analysis)) {
        return 0;
    }
    int result = 0;
    if (viewer_mode) {
        sci::app::Viewer viewer(game->manager, analysis, executor, config->mode, config->scale);
        event_loop(window.get(), renderer.get(), viewer);
        config->mode = viewer.mode();
        config->scale = viewer.chosen_scale();
    } else {
        sci::platform::HdPictures pictures(analysis, executor);
        sci::app::DirectoryFileStore files(base / "files", game->files);
        GameHost host(window.get(), renderer.get(), game->manager, pictures, *config);
        // Before the game, which sends it note-offs when it goes.
        const auto audio = open_audio(game->manager, *config);
        // A different seed every time, as ScummVM seeds its random source;
        // a replay's run starts from a known one.
        const std::uint32_t seed = options.replay
                                       ? options.seed.value_or(0)
                                       : options.seed.value_or(static_cast<std::uint32_t>(SDL_GetPerformanceCounter()));
        sci::vm::Game running(game->manager, host, &files, seed);
        // The saves next to the configuration, or in the configured folder
        // (docs/formats/save-format.md).
        sci::platform::SaveFolder saves(sci::platform::saves_folder(*config, base));
        running.set_saves(&saves, std::format("sierra-hd {}", SCI_VERSION));
        running.set_music_device(audio.get());
        host.set_audio(audio.get());
        host.set_fast(options.fast);
        running.schedule(replay);
        if (const auto fault = running.run()) {
            show_error(window.get(), std::format("The interpreter stopped: {}", *fault));
            result = 1;
        }
        if (options.end_state && !write_end_state(*options.end_state, running)) {
            std::println(stderr, "Cannot write {}", options.end_state->string());
            result = 1;
        }
    }
    std::ofstream(config_path) << sci::platform::format_config(*config);
    return result;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() == 2 && std::string_view(args[1]) == "--version") {
        std::println("sierra-hd {}", SCI_VERSION);
        return 0;
    }
    const auto options = parse_options(args);
    if (!options) {
        std::print("{}", help_text);
        return args.size() == 2 && std::string_view(args[1]) == "--help" ? 0 : 2;
    }
    sci::core::set_assertion_handler(&print_assertion);
    sci::core::set_log_sink(&print_log);
    // mt32emu's pitch jitter is the only user of rand() (the program's own
    // code never calls it); a fixed seed makes it the same in every run.
    // NOLINTBEGIN(bugprone-unsafe-functions,bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    std::srand(1);
    // NOLINTEND(bugprone-unsafe-functions,bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
    // Spec section 5.3: the floating-point profile, on the main thread too.
    if (const auto profile = sci::core::check_fp_profile(); !profile) {
        std::println(stderr, "{}", profile.error().message);
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "Cannot start SDL: {}", SDL_GetError());
        return 1;
    }
    const int result = run(*options);
    SDL_Quit();
    return result;
}
