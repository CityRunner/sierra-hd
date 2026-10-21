// SPDX-License-Identifier: GPL-3.0-or-later
//
// sierra-hd, release v0.1: the room viewer in a window (spec section 12,
// milestone 3a). The interpreter itself follows from milestone 4.
//
//   sierra-hd [game folder]
//   sierra-hd --version | --help
//
// The configuration file sierra-hd.ini and the hint files (hints/) are
// next to the executable (spec section 10.2).

#include "sci/core/assert.hpp"
#include "sci/core/fp_profile.hpp"
#include "sci/core/log.hpp"
#include "sci/platform/config.hpp"
#include "sci/platform/game_files.hpp"
#include "sci/platform/identify.hpp"
#include "sci/platform/thread_executor.hpp"
#include "viewer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
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

namespace {

namespace fs = std::filesystem;
using sci::app::Command;

constexpr std::string_view help_text = "usage: sierra-hd [game folder]\n"
                                       "       sierra-hd --version | --help\n"
                                       "\n"
                                       "Keys:\n"
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
        const std::vector<std::uint8_t>& frame = viewer.frame();
        const int width = sci::app::screen_width * viewer.scale();
        const int height = sci::app::screen_height * viewer.scale();
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
    }

private:
    SDL_Renderer* renderer_;
    std::unique_ptr<SDL_Texture, SdlDeleter> texture_;
    int width_ = 0;
    int height_ = 0;
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
    auto manager = sci::resource::ResourceManager::open(std::move(*files));
    if (!manager) {
        show_error(window, std::format("Cannot open the game: {}", manager.error().message));
        return std::nullopt;
    }
    return Game{.manager = std::move(*manager), .identification = std::move(*identification)};
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

int run(std::span<char*> args) {
    const fs::path base = base_path();
    const fs::path config_path = base / "sierra-hd.ini";
    auto config = load_config(config_path);
    if (!config) {
        return 1;
    }
    if (args.size() == 2) {
        config->game_folder = fs::path(args[1]);
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
    sci::app::Viewer viewer(game->manager, hint_directory(base), executor, config->mode, config->scale);
    event_loop(window.get(), renderer.get(), viewer);
    config->mode = viewer.mode();
    config->scale = viewer.chosen_scale();
    std::ofstream(config_path) << sci::platform::format_config(*config);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() == 2 && std::string_view(args[1]) == "--version") {
        std::println("sierra-hd {}", SCI_VERSION);
        return 0;
    }
    if (args.size() > 2 || (args.size() == 2 && std::string_view(args[1]).starts_with('-'))) {
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
