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
//   sci-hd-frames <game folder> <output folder> --check-interface <from>-<to>
//                 --stop-tick <n> [--replay <file>] [--seed <n>]
//                 [--hints <directory>] [--cache <directory>]
//
// Each tick gives <output>/<tick>.png (with --stack: stack-<n>+<n>.png):
// original | HD, and with --snap a third panel, HD with priority_snap.
// --timing composes the HD frame whenever the shown screen changes between
// the two ticks, as the program presents it, and prints the time taken.
// --check-interface (milestone 6b) checks every frame whose interface
// changed, in original and HD mode at every N from <from> to <to>: the
// interface and the menu bar are the shown 1x screen in N x N blocks, and
// the picture around them does not change with them
// (tools/common/interface_check.hpp); it writes nothing.

#include "game_folder.hpp"
#include "interface_check.hpp"
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
#include "sci/vm/local_time.hpp"

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
#include <list>
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
    std::optional<std::pair<int, int>> check_interface;
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
                         "                     [--snap] [--hints <directory>] [--cache <directory>]\n"
                         "       sci-hd-frames <game folder> <output folder> --check-interface <from>-<to>\n"
                         "                     --stop-tick <n> [--replay <file>] [--seed <n>]\n"
                         "                     [--hints <directory>] [--cache <directory>]");
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
        } else if (flag == "--check-interface") {
            const auto range = parse_list(value, '-');
            if (!range || range->size() != 2 || (*range)[0] < 1 || (*range)[0] > (*range)[1] ||
                std::cmp_greater((*range)[1], sci::platform::max_scale)) {
                return std::nullopt;
            }
            options.check_interface = std::pair{static_cast<int>((*range)[0]), static_cast<int>((*range)[1])};
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
    if (options.check_interface) {
        return options.ticks.empty() && options.stack.empty() && options.stop_tick ? std::optional(options)
                                                                                   : std::nullopt;
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

// The interface checks of milestone 6b on the frames whose interface
// changed: at every N of the range, the frame of each mode as the program
// keeps it up to date (sci/platform/game_frame.hpp), and in HD mode the
// frame updated to the same screen with the interface recoloured.
class InterfaceCheck {
public:
    InterfaceCheck(sci::platform::AnalysisCache& analysis, sci::core::Executor& executor, std::pair<int, int> scales)
        : scales_(scales) {
        for (int scale = scales.first; scale <= scales.second; ++scale) {
            per_scale_.emplace_back(analysis, executor, scale);
        }
    }

    void check(const sci::vm::ShownScreen& shown) {
        if (shown.visual.size() != screen_pixels || shown.serial == last_serial_) {
            return;
        }
        last_serial_ = shown.serial;
        const std::uint64_t key = interface_key(shown);
        if (key == last_key_ && std::ranges::equal(shown.pictures, last_pictures_)) {
            return;
        }
        last_key_ = key;
        last_pictures_.assign(shown.pictures.begin(), shown.pictures.end());
        ++frames_;
        const bool below_bar = std::ranges::any_of(shown.owner.subspan(std::size_t{screen_width} * 10), [](auto owner) {
            return owner == static_cast<std::uint8_t>(sci::vm::Owner::Interface);
        });
        windows_ += below_bar ? 1 : 0;
        over_sprites_ += sci::tools::window_over_sprite(shown) ? 1 : 0;
        report(0, "menu bar", sci::tools::check_menu_bar(shown));
        const std::vector<std::uint8_t> recoloured_visual = sci::tools::recolour_interface(shown);
        sci::vm::ShownScreen recoloured = shown;
        recoloured.visual = recoloured_visual;
        for (Scale& entry : per_scale_) {
            entry.original.update(shown, GraphicsMode::Original, entry.scale, false);
            report(entry.scale, "original",
                   sci::tools::check_interface_blocks(shown, entry.original.frame(), entry.scale, false));
            entry.hd.update(shown, GraphicsMode::Hd, entry.scale, false);
            report(entry.scale, "HD", sci::tools::check_interface_blocks(shown, entry.hd.frame(), entry.scale, true));
            // The probe shows the screen, then the screen with the interface
            // recoloured: as in the program, the tiles that changed are
            // composed again.
            entry.probe.update(shown, GraphicsMode::Hd, entry.scale, false);
            entry.probe.update(recoloured, GraphicsMode::Hd, entry.scale, false);
            report(entry.scale, "HD, recoloured interface",
                   sci::tools::check_isolation(shown, entry.hd.frame(), entry.probe.frame(), entry.scale));
            ++checks_;
        }
    }

    // Prints the result; false if any check failed.
    [[nodiscard]] bool print() const {
        std::println("interface check: {} frames ({} with interface below the menu bar, {} with a window over a "
                     "sprite), N = {} to {}, original and HD mode: {} frame checks, {} failed",
                     frames_, windows_, over_sprites_, scales_.first, scales_.second, checks_, failures_);
        return failures_ == 0;
    }

    void set_tick(std::uint32_t tick) { tick_ = tick; }

private:
    struct Scale {
        Scale(sci::platform::AnalysisCache& analysis, sci::core::Executor& executor, int n)
            : scale(n), pictures(analysis, executor), original(source()), hd(source()), probe(source()) {}

        // The frames' sources point to this Scale.
        Scale(const Scale&) = delete;
        Scale& operator=(const Scale&) = delete;
        Scale(Scale&&) = delete;
        Scale& operator=(Scale&&) = delete;
        ~Scale() = default;

        // The HD images at this N (each Scale stays where the list put it).
        sci::platform::HdSource source() {
            return [this](auto stack, auto lines, int at) { return pictures.get(stack, lines, at); };
        }

        int scale;
        sci::platform::HdPictures pictures;
        GameFrame original;
        GameFrame hd;
        GameFrame probe;
    };

    // The interface's pixels and colours, to check each state once.
    static std::uint64_t interface_key(const sci::vm::ShownScreen& shown) {
        std::uint64_t hash = 0xcbf29ce484222325ULL; // FNV-1a.
        for (std::size_t i = 0; i < screen_pixels; ++i) {
            const bool mine = shown.owner[i] == static_cast<std::uint8_t>(sci::vm::Owner::Interface);
            hash = (hash ^ (mine ? shown.visual[i] : 0xFFU)) * 0x100000001b3ULL;
        }
        return hash;
    }

    void report(int scale, std::string_view mode, const std::optional<sci::tools::FrameProblem>& problem) {
        if (!problem) {
            return;
        }
        if (++failures_ <= 10) {
            std::println("tick {}, N = {}, {}: pixel ({}, {}): {}", tick_, scale, mode, problem->x, problem->y,
                         problem->what);
        }
    }

    std::pair<int, int> scales_;
    std::list<Scale> per_scale_;
    std::uint64_t last_serial_ = ~std::uint64_t{0};
    std::uint64_t last_key_ = 0;
    std::vector<sci::vm::ShownPicture> last_pictures_;
    std::uint32_t tick_ = 0;
    int frames_ = 0;
    int windows_ = 0;
    int over_sprites_ = 0;
    int checks_ = 0;
    int failures_ = 0;
};

class Host final : public sci::vm::GameHost {
public:
    Host(sci::platform::HdPictures& pictures, const Options& options, InterfaceCheck* check)
        : pictures_(&pictures), options_(&options), sheet_(pictures, options),
          timed_([&pictures](auto stack, auto lines, int scale) { return pictures.get(stack, lines, scale); }),
          check_(check) {}

    void wait(sci::vm::Game& game, std::uint32_t milliseconds) override {
        virtual_ms_ += milliseconds;
        const auto tick = static_cast<std::uint32_t>(virtual_ms_ * 60 / 1000);
        if (check_ != nullptr) {
            check_->set_tick(tick);
            check_->check(game.screen());
        }
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

    // The deterministic mode's time of day, as in sci-vm-trace and the
    // reference: scenes timed by the time of day run as there.
    [[nodiscard]] sci::vm::LocalTime local_time() override {
        return sci::vm::deterministic_local_time(virtual_ms_ * 1000);
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
    InterfaceCheck* check_;
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
    std::optional<InterfaceCheck> check;
    if (options->check_interface) {
        check.emplace(analysis, executor, *options->check_interface);
    }
    Host host(pictures, *options, check ? &*check : nullptr);
    sci::vm::Game game(*manager, host, &files, options->seed);
    // Saves in memory: every run starts with none, as sci-vm-trace's do, so
    // a replay that saves and restores in the game renders the same frames.
    sci::vm::MemorySaveStore saves;
    game.set_saves(&saves, "sci-hd-frames");
    game.schedule(input);
    if (const auto fault = game.run()) {
        std::println(stderr, "the interpreter stopped: {}", *fault);
        return 1;
    }
    if (options->timing) {
        host.timing().print(options->scale);
    }
    if (check && !check->print()) {
        return 1;
    }
    return 0;
}
