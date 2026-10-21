// SPDX-License-Identifier: GPL-3.0-or-later
//
// sierra-hd: the game in a window (milestone 4b), and the room viewer of
// release v0.1 (milestone 3a) with --viewer.
//
//   sierra-hd [--viewer] [game folder]
//   sierra-hd --version | --help
//
// The configuration file sierra-hd.ini and the hint files (hints/) are
// next to the executable (spec section 10.2).

#include "file_store.hpp"
#include "game_screen.hpp"
#include "hd_pictures.hpp"
#include "sci/core/assert.hpp"
#include "sci/core/fp_profile.hpp"
#include "sci/core/log.hpp"
#include "sci/platform/config.hpp"
#include "sci/platform/game_files.hpp"
#include "sci/platform/identify.hpp"
#include "sci/platform/thread_executor.hpp"
#include "sci/vm/game.hpp"
#include "viewer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// SDL's window coordinates (single precision). They only place the pointer;
// nothing that is drawn depends on them, and arithmetic on them is in double.
using SdlCoordinate = decltype(SDL_FPoint::x);

namespace fs = std::filesystem;
using sci::app::Command;

constexpr std::string_view help_text = "usage: sierra-hd [--viewer] [game folder]\n"
                                       "       sierra-hd --version | --help\n"
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
};

// Presents the viewer's frame: uploads the changed rectangles to the
// texture and draws it centred, letterboxed, at 1:1 in physical pixels.
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
        SDL_RenderPresent(renderer_);
        offset_x_ = target.x;
        offset_y_ = target.y;
    }

    // Where the frame starts in the output, in physical pixels.
    [[nodiscard]] int offset_x() const { return offset_x_; }

    [[nodiscard]] int offset_y() const { return offset_y_; }

private:
    SDL_Renderer* renderer_;
    std::unique_ptr<SDL_Texture, SdlDeleter> texture_;
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
        .manager = std::move(*manager), .identification = std::move(*identification), .files = std::move(copies)};
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

    static constexpr std::array<Keypad, 16> keypad{{
        {SDLK_KP_0, Key::Insert, '0'},
        {SDLK_KP_1, Key::End, '1'},
        {SDLK_KP_2, Key::Down, '2'},
        {SDLK_KP_3, Key::PageDown, '3'},
        {SDLK_KP_4, Key::Left, '4'},
        {SDLK_KP_5, Key::Center, '5'},
        {SDLK_KP_6, Key::Right, '6'},
        {SDLK_KP_7, Key::Home, '7'},
        {SDLK_KP_8, Key::Up, '8'},
        {SDLK_KP_9, Key::PageUp, '9'},
        {SDLK_KP_PERIOD, Key::Delete, '.'},
        {SDLK_KP_ENTER, Key::Enter, 0},
        {SDLK_KP_PLUS, Key::Character, '+'},
        {SDLK_KP_MINUS, Key::Character, '-'},
        {SDLK_KP_MULTIPLY, Key::Character, '*'},
        {SDLK_KP_DIVIDE, Key::Character, '/'},
    }};
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
    GameHost(SDL_Window* window, SDL_Renderer* renderer, const sci::resource::ResourceManager& manager,
             sci::app::HdPictures& pictures, sci::platform::Config& config)
        : window_(window), renderer_(renderer), manager_(&manager), presenter_(renderer), config_(&config),
          screen_([&pictures](int picture, bool mirrored, int palette, int scale) {
              return pictures.get(picture, mirrored, palette, scale);
          }),
          start_(SDL_GetTicks()) {}

    void wait(sci::vm::Game& game, std::uint32_t milliseconds) override {
        virtual_ += milliseconds;
        while (true) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                handle(game, event);
            }
            present(game);
            const std::uint64_t now = SDL_GetTicks() - start_;
            if (now >= virtual_) {
                // Far behind (a slow machine, a dialog): catch up, no burst.
                if (now > virtual_ + 250) {
                    virtual_ = now;
                }
                return;
            }
            SDL_Delay(static_cast<std::uint32_t>(std::min<std::uint64_t>(virtual_ - now, 4)));
        }
    }

private:
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
            last_mouse_ = to_1x(event.motion.x, event.motion.y);
            game.move_mouse(last_mouse_);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            last_mouse_ = to_1x(event.button.x, event.button.y);
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

    // A window position in 1x screen coordinates: the screen pixel minus the
    // letterbox, divided by N (spec section 6.9).
    [[nodiscard]] sci::vm::Point to_1x(SdlCoordinate x, SdlCoordinate y) const {
        const auto density = static_cast<double>(SDL_GetWindowPixelDensity(window_));
        const int scale = std::max(screen_.scale(), 1);
        const int px = static_cast<int>(static_cast<double>(x) * density) - presenter_.offset_x();
        const int py = static_cast<int>(static_cast<double>(y) * density) - presenter_.offset_y();
        return sci::vm::Point{
            .x = static_cast<std::int16_t>(std::clamp(px / scale, 0, sci::app::screen_width - 1)),
            .y = static_cast<std::int16_t>(std::clamp(py / scale, 0, sci::app::screen_height - 1)),
        };
    }

    void present(sci::vm::Game& game) {
        const sci::vm::ShownScreen shown = game.screen();
        const int scale = scale_for_window();
        if (!redraw_ && shown.serial == last_serial_ && scale == screen_.scale()) {
            update_cursor(shown, scale);
            return;
        }
        screen_.update(shown, config_->mode, scale);
        presenter_.present(screen_.frame(), screen_.scale(), redraw_ ? full_frame() : screen_.take_dirty());
        if (redraw_) {
            (void)screen_.take_dirty();
        }
        redraw_ = false;
        last_serial_ = shown.serial;
        update_cursor(shown, scale);
    }

    [[nodiscard]] std::vector<sci::app::Rect> full_frame() const {
        return {sci::app::Rect{.x = 0,
                               .y = 0,
                               .width = sci::app::screen_width * screen_.scale(),
                               .height = sci::app::screen_height * screen_.scale()}};
    }

    // The game's cursor as the window's cursor, scaled N x N, so it moves
    // smoothly; and where the game put the mouse, the pointer goes too.
    void update_cursor(const sci::vm::ShownScreen& shown, int scale) {
        if (shown.mouse != last_mouse_) {
            const auto density = static_cast<double>(SDL_GetWindowPixelDensity(window_));
            const double x = (presenter_.offset_x() + (shown.mouse.x * scale) + (scale / 2)) / density;
            const double y = (presenter_.offset_y() + (shown.mouse.y * scale) + (scale / 2)) / density;
            SDL_WarpMouseInWindow(window_, static_cast<SdlCoordinate>(x), static_cast<SdlCoordinate>(y));
            last_mouse_ = shown.mouse;
        }
        if (shown.cursor == cursor_shape_ && scale == cursor_scale_) {
            return;
        }
        cursor_shape_ = shown.cursor;
        cursor_scale_ = scale;
        if (shown.cursor < 0) {
            SDL_HideCursor();
            return;
        }
        const auto resource = manager_->load(sci::resource::ResourceId{
            .type = sci::resource::ResourceType::Cursor, .number = static_cast<std::uint16_t>(shown.cursor)});
        const auto image = resource ? sci::app::decode_cursor(resource->data) : std::nullopt;
        if (!image) {
            SDL_HideCursor();
            return;
        }
        const int size = 16 * scale;
        SDL_Surface* surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return;
        }
        const std::span<std::uint8_t> pixels(static_cast<std::uint8_t*>(surface->pixels),
                                             static_cast<std::size_t>(surface->pitch) * static_cast<std::size_t>(size));
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const std::size_t from =
                    ((static_cast<std::size_t>(y / scale) * 16) + static_cast<std::size_t>(x / scale)) * 4;
                const std::size_t to = (static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch)) +
                                       (static_cast<std::size_t>(x) * 4);
                std::copy_n(image->pixels.begin() + static_cast<std::ptrdiff_t>(from), 4,
                            pixels.begin() + static_cast<std::ptrdiff_t>(to));
            }
        }
        SDL_Cursor* cursor = SDL_CreateColorCursor(surface, image->hotspot_x * scale, image->hotspot_y * scale);
        SDL_DestroySurface(surface);
        if (cursor != nullptr) {
            SDL_SetCursor(cursor);
            if (cursor_ != nullptr) {
                SDL_DestroyCursor(cursor_);
            }
            cursor_ = cursor;
            SDL_ShowCursor();
        }
    }

    SDL_Window* window_;
    SDL_Renderer* renderer_;
    const sci::resource::ResourceManager* manager_;
    Presenter presenter_;
    sci::platform::Config* config_;
    sci::app::GameScreen screen_;
    std::uint64_t start_;
    std::uint64_t virtual_ = 0;
    std::uint64_t last_serial_ = ~std::uint64_t{0};
    bool redraw_ = true;
    sci::vm::Point last_mouse_{.x = 160, .y = 100};
    int cursor_shape_ = -2;
    int cursor_scale_ = 0;
    SDL_Cursor* cursor_ = nullptr;
};

int run(std::span<char*> args) {
    const fs::path base = base_path();
    const fs::path config_path = base / "sierra-hd.ini";
    auto config = load_config(config_path);
    if (!config) {
        return 1;
    }
    bool viewer_mode = false;
    for (const char* arg : args.subspan(1)) {
        if (std::string_view(arg) == "--viewer") {
            viewer_mode = true;
        } else {
            config->game_folder = fs::path(arg);
        }
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
    int result = 0;
    if (viewer_mode) {
        sci::app::Viewer viewer(game->manager, hint_directory(base), executor, config->mode, config->scale);
        event_loop(window.get(), renderer.get(), viewer);
        config->mode = viewer.mode();
        config->scale = viewer.chosen_scale();
    } else {
        sci::app::HdPictures pictures(game->manager, hint_directory(base), executor);
        sci::app::DirectoryFileStore files(base / "files", game->files);
        GameHost host(window.get(), renderer.get(), game->manager, pictures, *config);
        // A different seed every time, as ScummVM seeds its random source.
        sci::vm::Game running(game->manager, host, &files, static_cast<std::uint32_t>(SDL_GetPerformanceCounter()));
        if (const auto fault = running.run()) {
            show_error(window.get(), std::format("The interpreter stopped: {}", *fault));
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
    const auto is_option = [](const char* arg) {
        return std::string_view(arg).starts_with('-') && std::string_view(arg) != "--viewer";
    };
    if (args.size() > 3 || std::ranges::any_of(args.subspan(1), is_option)) {
        std::print("{}", help_text);
        return args.size() == 2 && std::string_view(args[1]) == "--help" ? 0 : 2;
    }
    sci::core::set_assertion_handler(&print_assertion);
    sci::core::set_log_sink(&print_log);
    // Spec section 5.3: the floating-point profile, on the main thread too.
    if (const auto profile = sci::core::check_fp_profile(); !profile) {
        std::println(stderr, "{}", profile.error().message);
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "Cannot start SDL: {}", SDL_GetError());
        return 1;
    }
    const int result = run(args);
    SDL_Quit();
    return result;
}
