// SPDX-License-Identifier: GPL-3.0-or-later
//
// Development tool (milestone 5): runs the game without a window, as the
// program does but with virtual time only, and writes the frames the
// program would present at chosen ticks: original mode and HD mode side by
// side at N, composed by the program's own code (sci/platform/game_frame.hpp).
// It also measures the composition time per presented frame. The run is
// deterministic: a replay file, a seed and virtual time.
//
//   sci-hd-frames <game folder> <output folder> --at <tick>[,<tick>...]
//                 [--replay <file>] [--seed <n>] [--scale <n>] [--snap]
//                 [--hints <directory>] [--cache <directory>]
//                 [--timing <from tick>-<to tick>] [--stop-tick <n>]
//   sci-hd-frames <game folder> <output folder> --stack <n>[+<n>...]
//                 [--scale <n>] [--snap] [--hints <directory>] [--cache <directory>]
//
// Each tick gives <output>/<tick>.png (with --stack: stack-<n>+<n>.png):
// original | HD, and with --snap a third panel, HD with priority_snap.
// --timing composes the HD frame whenever the shown screen changes between
// the two ticks, as the program presents it, and prints the time taken.

#include "game_folder.hpp"
#include "picture.hpp"
#include "png.hpp"
#include "sci/core/log.hpp"
#include "sci/platform/analysis_cache.hpp"
#include "sci/platform/config.hpp"
#include "sci/platform/game_frame.hpp"
#include "sci/platform/hd_pictures.hpp"
#include "sci/platform/thread_executor.hpp"
#include "sci/vm/events.hpp"
#include "sci/vm/files.hpp"
#include "sci/vm/game.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sci::platform::GameFrame;
using sci::platform::GraphicsMode;

constexpr int screen_width = 320;
constexpr int screen_height = 200;
constexpr std::size_t screen_pixels = static_cast<std::size_t>(screen_width) * screen_height;

struct Options {
    fs::path game;
    fs::path output;
    std::vector<std::uint32_t> ticks;
    std::vector<std::uint16_t> stack;
    std::optional<fs::path> replay;
    std::uint32_t seed = 0;
    int scale = 5;
    bool snap = false;
    std::optional<fs::path> hints;
    std::optional<fs::path> cache;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> timing;
    std::optional<std::uint32_t> stop_tick;
};

std::optional<std::uint32_t> parse_number(std::string_view text) {
    std::uint32_t value = 0;
    const char* const last = std::to_address(text.end());
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), last, value);
    if (error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

// "a<separator>b<separator>c" as numbers.
std::optional<std::vector<std::uint32_t>> parse_list(std::string_view text, char separator) {
    std::vector<std::uint32_t> values;
    while (true) {
        const auto at = text.find(separator);
        const auto value = parse_number(text.substr(0, at));
        if (!value) {
            return std::nullopt;
        }
        values.push_back(*value);
        if (at == std::string_view::npos) {
            return values;
        }
        text = text.substr(at + 1);
    }
}

int usage() {
    std::println(stderr, "usage: sci-hd-frames <game folder> <output folder> --at <tick>[,<tick>...]\n"
                         "                     [--replay <file>] [--seed <n>] [--scale <n>] [--snap]\n"
                         "                     [--hints <directory>] [--cache <directory>]\n"
                         "                     [--timing <from>-<to>] [--stop-tick <n>]\n"
                         "       sci-hd-frames <game folder> <output folder> --stack <n>[+<n>...] [--scale <n>]\n"
                         "                     [--snap] [--hints <directory>] [--cache <directory>]");
    return 2;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): argument parsing.
std::optional<Options> parse_options(std::span<char*> args) {
    if (args.size() < 3) {
        return std::nullopt;
    }
    Options options;
    options.game = args[1];
    options.output = args[2];
    for (std::size_t i = 3; i < args.size(); ++i) {
        const std::string_view flag = args[i];
        if (flag == "--snap") {
            options.snap = true;
            continue;
        }
        if (i + 1 >= args.size()) {
            return std::nullopt;
        }
        const std::string_view value = args[++i];
        if (flag == "--at") {
            const auto ticks = parse_list(value, ',');
            if (!ticks) {
                return std::nullopt;
            }
            options.ticks.insert(options.ticks.end(), ticks->begin(), ticks->end());
        } else if (flag == "--stack") {
            const auto numbers = parse_list(value, '+');
            if (!numbers) {
                return std::nullopt;
            }
            for (const std::uint32_t number : *numbers) {
                options.stack.push_back(static_cast<std::uint16_t>(number));
            }
        } else if (flag == "--replay") {
            options.replay = fs::path(value);
        } else if (flag == "--hints") {
            options.hints = fs::path(value);
        } else if (flag == "--cache") {
            options.cache = fs::path(value);
        } else if (flag == "--timing") {
            const auto range = parse_list(value, '-');
            if (!range || range->size() != 2) {
                return std::nullopt;
            }
            options.timing = std::pair{(*range)[0], (*range)[1]};
        } else if (const auto number = parse_number(value)) {
            if (flag == "--seed") {
                options.seed = *number;
            } else if (flag == "--scale" && *number >= 1 && *number <= sci::platform::max_scale) {
                options.scale = static_cast<int>(*number);
            } else if (flag == "--stop-tick") {
                options.stop_tick = number;
            } else {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    if (options.ticks.empty() == options.stack.empty()) {
        return std::nullopt;
    }
    std::ranges::stable_sort(options.ticks);
    return options;
}

// The panels side by side: original, HD, and with snapping HD snapped.
class Sheet {
public:
    Sheet(sci::platform::HdPictures& pictures, const Options& options)
        : pictures_(&pictures), options_(&options),
          image_((screen_width * options.scale * (options.snap ? 3 : 2)) + ((options.snap ? 2 : 1) * gap),
                 screen_height * options.scale, sci::tools::Rgb{.r = 0x40, .g = 0x40, .b = 0x40}) {}

    bool write(const sci::vm::ShownScreen& shown, const fs::path& path) {
        int panel = 0;
        const auto draw = [&](GraphicsMode mode, bool snap) {
            GameFrame frame([this](auto stack, auto lines, int scale) { return pictures_->get(stack, lines, scale); });
            frame.update(shown, mode, options_->scale, snap);
            copy(frame.frame(), panel++);
        };
        draw(GraphicsMode::Original, false);
        draw(GraphicsMode::Hd, false);
        if (options_->snap) {
            draw(GraphicsMode::Hd, true);
        }
        return image_.write_png(path);
    }

private:
    static constexpr int gap = 8;

    void copy(const std::vector<std::uint8_t>& frame, int panel) {
        const int width = screen_width * options_->scale;
        const int height = screen_height * options_->scale;
        if (frame.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
            return;
        }
        const int left = panel * (width + gap);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t i =
                    ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)) * 4;
                image_.set(left + x, y, sci::tools::Rgb{.r = frame[i], .g = frame[i + 1], .b = frame[i + 2]});
            }
        }
    }

    sci::platform::HdPictures* pictures_;
    const Options* options_;
    sci::tools::Image image_;
};

// The composition times of the presented frames.
struct Timing {
    std::vector<double> all;         // Milliseconds per frame.
    std::vector<double> incremental; // Frames composed in part.
    std::size_t pixels = 0;          // 1x pixels composed in the incremental frames.
    std::optional<double> full;      // A whole frame, the HD images ready.

    void print(int scale) const {
        const auto stats = [](std::vector<double> times) {
            if (times.empty()) {
                return std::string("none");
            }
            std::ranges::stable_sort(times);
            double sum = 0;
            for (const double t : times) {
                sum += t;
            }
            return std::format("{} frames, mean {:.3f} ms, median {:.3f} ms, 99th percentile {:.3f} ms, max {:.3f} ms",
                               times.size(), sum / static_cast<double>(times.size()), times[times.size() / 2],
                               times[(times.size() * 99) / 100], times.back());
        };
        std::println("composition at N = {} (the first frame includes rendering the HD images):", scale);
        std::println("  all frames:         {}", stats(all));
        std::println("  incremental frames: {}", stats(incremental));
        if (!incremental.empty()) {
            std::println("  1x pixels composed per incremental frame: mean {}", pixels / incremental.size());
        }
        if (full) {
            std::println("  a whole frame composed again: {:.3f} ms", *full);
        }
    }
};

class Host final : public sci::vm::GameHost {
public:
    Host(sci::platform::HdPictures& pictures, const Options& options)
        : pictures_(&pictures), options_(&options), sheet_(pictures, options),
          timed_([&pictures](auto stack, auto lines, int scale) { return pictures.get(stack, lines, scale); }) {}

    void wait(sci::vm::Game& game, std::uint32_t milliseconds) override {
        virtual_ms_ += milliseconds;
        const auto tick = static_cast<std::uint32_t>(virtual_ms_ * 60 / 1000);
        if (options_->timing && tick >= options_->timing->first && tick <= options_->timing->second) {
            time_frame(game.screen());
        }
        if (options_->timing && tick > options_->timing->second && !timing_.full) {
            time_full_frame(game.screen());
        }
        while (next_ < options_->ticks.size() && tick >= options_->ticks[next_]) {
            const fs::path path = options_->output / std::format("{:06}.png", options_->ticks[next_]);
            if (!sheet_.write(game.screen(), path)) {
                std::println(stderr, "cannot write {}", path.string());
            }
            ++next_;
        }
        const std::uint32_t last = options_->stop_tick.value_or(std::max(
            options_->ticks.empty() ? 0 : options_->ticks.back(), options_->timing ? options_->timing->second : 0));
        if (tick >= last && !quitting_) {
            quitting_ = true;
            game.quit();
        }
    }

    [[nodiscard]] sci::vm::LocalTime local_time() override {
        return sci::vm::LocalTime{.year = 89, .month = 0, .day = 1, .hour = 12, .minute = 0, .second = 0};
    }

    [[nodiscard]] const Timing& timing() const { return timing_; }

private:
    // A whole frame composed again, the HD images already rendered (as
    // after a switch of mode or of the snapping).
    void time_full_frame(const sci::vm::ShownScreen& shown) {
        (void)pictures_->get(shown.pictures, shown.lines, options_->scale);
        GameFrame fresh([this](auto stack, auto lines, int scale) { return pictures_->get(stack, lines, scale); });
        const auto start = std::chrono::steady_clock::now();
        fresh.update(shown, GraphicsMode::Hd, options_->scale, options_->snap);
        const std::chrono::duration<double, std::milli> taken = std::chrono::steady_clock::now() - start;
        timing_.full = taken.count();
    }

    void time_frame(const sci::vm::ShownScreen& shown) {
        if (shown.serial == last_serial_) {
            return; // The program presents nothing new either.
        }
        last_serial_ = shown.serial;
        const auto start = std::chrono::steady_clock::now();
        timed_.update(shown, GraphicsMode::Hd, options_->scale, options_->snap);
        const std::chrono::duration<double, std::milli> taken = std::chrono::steady_clock::now() - start;
        (void)timed_.take_dirty();
        timing_.all.push_back(taken.count());
        if (timed_.last_composed() < screen_pixels) {
            timing_.incremental.push_back(taken.count());
            timing_.pixels += timed_.last_composed();
        }
    }

    sci::platform::HdPictures* pictures_;
    const Options* options_;
    Sheet sheet_;
    GameFrame timed_;
    Timing timing_;
    std::uint64_t virtual_ms_ = 0;
    std::size_t next_ = 0;
    bool quitting_ = false;
    std::uint64_t last_serial_ = ~std::uint64_t{0};
};

// The picture stack as the interpreter would show it: drawn by path 1, the
// picture's everywhere, nothing else.
int write_stack(const sci::resource::ResourceManager& manager, sci::platform::HdPictures& pictures,
                const Options& options) {
    const auto drawn = sci::tools::draw_pictures(manager, options.stack);
    if (!drawn) {
        return 1;
    }
    std::vector<std::uint8_t> owner(screen_pixels, static_cast<std::uint8_t>(sci::vm::Owner::Picture));
    std::vector<std::uint8_t> source(screen_pixels, sci::vm::priority_from_picture);
    for (std::size_t i = 0; i < static_cast<std::size_t>(screen_width) * 10; ++i) {
        owner[i] = static_cast<std::uint8_t>(sci::vm::Owner::Interface);
    }
    const std::vector<std::uint32_t> layer(screen_pixels, 0);
    std::vector<sci::vm::ShownPicture> stack;
    stack.reserve(options.stack.size());
    for (const std::uint16_t number : options.stack) {
        stack.push_back(sci::vm::ShownPicture{.number = number, .mirrored = false, .ega_palette = 0});
    }
    const sci::vm::ShownScreen shown{
        .visual = drawn->maps.visual,
        .serial = 1,
        .owner = owner,
        .background_visual = drawn->maps.visual,
        .background_owner = owner,
        .background_priority = drawn->maps.priority,
        .background_source = source,
        .sprite_layer = layer,
        .sprite_lists = {},
        .pictures = stack,
        .lines = {},
        .cursor = -1,
        .mouse = sci::vm::Point{.x = 0, .y = 0},
    };
    std::string name = "stack";
    for (std::size_t i = 0; i < options.stack.size(); ++i) {
        name += std::format("{}{:03}", i == 0 ? "-" : "+", options.stack[i]);
    }
    Sheet sheet(pictures, options);
    const fs::path path = options.output / (name + ".png");
    if (!sheet.write(shown, path)) {
        std::println(stderr, "cannot write {}", path.string());
        return 1;
    }
    return 0;
}

std::optional<std::vector<sci::vm::InputEvent>> read_replay(const fs::path& path) {
    const std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    if (!file) {
        std::println(stderr, "cannot read {}", path.string());
        return std::nullopt;
    }
    auto events = sci::vm::parse_replay(text.str());
    if (!events) {
        std::println(stderr, "{}: {}", path.string(), events.error().message);
        return std::nullopt;
    }
    return std::move(*events);
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(std::span(argv, static_cast<std::size_t>(argc)));
    if (!options) {
        return usage();
    }
    sci::tools::install_stderr_log();
    const auto manager = sci::tools::open_game(options->game);
    if (!manager) {
        return 1;
    }
    std::error_code error;
    fs::create_directories(options->output, error);
    sci::platform::ThreadExecutor executor;
    sci::platform::AnalysisCache analysis(*manager, options->hints, options->cache, executor);
    sci::platform::HdPictures pictures(analysis, executor);
    if (!options->stack.empty()) {
        return write_stack(*manager, pictures, *options);
    }
    std::vector<sci::vm::InputEvent> input;
    if (options->replay) {
        auto events = read_replay(*options->replay);
        if (!events) {
            return 1;
        }
        input = std::move(*events);
    }
    // The game folder's files for kFOpen to read, under lower-case names,
    // and nothing written yet, as in sci-vm-trace.
    std::map<std::string, std::vector<std::uint8_t>> game_files;
    for (const auto& entry : fs::directory_iterator(options->game, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream file{entry.path(), std::ios::binary};
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        std::string name = entry.path().filename().string();
        std::ranges::transform(name, name.begin(),
                               [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        game_files.emplace(std::move(name), std::move(bytes));
    }
    sci::vm::MemoryFileStore files(std::move(game_files));
    Host host(pictures, *options);
    sci::vm::Game game(*manager, host, &files, options->seed);
    game.schedule(input);
    if (const auto fault = game.run()) {
        std::println(stderr, "the interpreter stopped: {}", *fault);
        return 1;
    }
    if (options->timing) {
        host.timing().print(options->scale);
    }
    return 0;
}
