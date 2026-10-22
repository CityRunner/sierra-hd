// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden hashes of every picture's background (spec section 5.3): SHA-256
// of the HD frame (8-bit RGBA, picture area) at N = 1, 2, 5 and 10, and of
// the original-mode frame (palette indices) at N = 1. They catch changes,
// not quality, and must be the same on every machine.
//
//   sci-golden-hashes <game> <file> [--hints <directory>]           writes the hashes
//   sci-golden-hashes <game> <file> --check [--hints <directory>]   compares with the file
//
// With --cache <directory>, the HD scenes come from the analysis cache in
// that directory (spec section 10.2): analysed and written if missing, read
// back otherwise; the hashes must not change. --prepare first analyses the
// missing entries as the program's first launch does. Both report times.
//
// The recorded hashes are made with the repository's hints (hints/).
//
// One line per picture: "pic <n> <kind> <hash>" for each kind. Then the
// picture stacks the game's scripts draw (a picture with overlays drawn
// over it by kDrawPic with add-to; docs/ASSUMPTIONS.md, "Milestone 5: the
// picture stacks"): "stack <stack> <kind> <hash>", where <stack> is the
// base picture, then each overlay, joined by "+", each with ".mirrored"
// and ".palette<n>" for its kDrawPic options as the analysis cache names
// its entries ("174.palette1+74"). The HD frames of a stack come from the
// analysis cache's variant of the stack, as the program draws them.

#include "game_folder.hpp"
#include "picture.hpp"
#include "sci/core/sha256.hpp"
#include "sci/pic/picture_maps.hpp"
#include "sci/platform/analysis_cache.hpp"
#include "sci/platform/picture_hints.hpp"
#include "sci/platform/thread_executor.hpp"
#include "sci/render/original.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <print>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array hd_scales{1, 2, 5, 10};

std::string hash(std::span<const std::uint8_t> bytes) {
    return sci::core::to_hex(sci::core::sha256(bytes));
}

// The lines of one background: the HD frames at each N, the original frame.
std::string frame_hashes(std::string_view prefix, const sci::vector::Scene& visual, const sci::pic::PictureMaps& maps,
                         sci::core::Executor& executor) {
    std::string lines;
    for (const int scale : hd_scales) {
        const auto frame = sci::render::render_hd(visual, scale, executor);
        lines += std::format("{} hd{} {}\n", prefix, scale, hash(frame.pixels));
    }
    lines += std::format("{} original1 {}\n", prefix, hash(sci::render::render_original(maps, 1)));
    return lines;
}

std::string picture_hashes(const sci::resource::ResourceManager& manager, std::uint16_t number,
                           const std::optional<std::filesystem::path>& hint_directory, sci::core::Executor& executor,
                           sci::platform::AnalysisCache* cache, Clock::duration& in_cache) {
    const std::array numbers{number};
    const auto drawn = sci::tools::draw_pictures(manager, numbers);
    if (!drawn) {
        return std::format("pic {} error\n", number);
    }
    sci::vector::Scene visual;
    if (cache != nullptr) {
        const auto start = Clock::now();
        const auto cached = cache->scene(sci::platform::SceneVariant{.picture = number});
        in_cache += Clock::now() - start;
        if (!cached) {
            return std::format("pic {} error\n", number);
        }
        visual = **cached;
    } else {
        const sci::platform::PictureHintSet hints(hint_directory, manager, numbers);
        visual =
            sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Visual, hints.pointers()).scene;
    }
    return frame_hashes(std::format("pic {}", number), visual, drawn->maps, executor);
}

using sci::platform::PictureOverlay;
using sci::platform::SceneVariant;

// The picture stacks the scripts draw, with the options they draw them with
// (docs/ASSUMPTIONS.md, "Milestone 5: the picture stacks").
std::vector<SceneVariant> script_stacks() {
    return {
        // Room 74, its two local procedures: 174 with palette 0 or 1, then
        // 74 over it with palette 0 (script 074 at 002d and 0125).
        SceneVariant{.picture = 174, .overlays = {PictureOverlay{.picture = 74}}},
        SceneVariant{.picture = 174, .ega_palette = 1, .overlays = {PictureOverlay{.picture = 74}}},
        // Game::replay after restoring a game saved in room 74 (script 994 at
        // 01e5): the room's curPic 74, then global 57 (174) over it, both
        // with global 61's palette, 0 or 1.
        SceneVariant{.picture = 74, .overlays = {PictureOverlay{.picture = 174}}},
        SceneVariant{.picture = 74, .ega_palette = 1, .overlays = {PictureOverlay{.picture = 174, .ega_palette = 1}}},
        // Room 290: 282, then local 0 over it, 182 or 382 (script 290 at 01c1).
        SceneVariant{.picture = 282, .overlays = {PictureOverlay{.picture = 182}}},
        SceneVariant{.picture = 282, .overlays = {PictureOverlay{.picture = 382}}},
        // Room 353: 42, then 162 over it (script 353 at 005b).
        SceneVariant{.picture = 42, .overlays = {PictureOverlay{.picture = 162}}},
    };
}

std::string picture_name(std::uint16_t number, bool mirrored, std::uint8_t ega_palette) {
    std::string name = std::to_string(number);
    if (mirrored) {
        name += ".mirrored";
    }
    if (ega_palette != 0) {
        name += std::format(".palette{}", ega_palette);
    }
    return name;
}

// "174.palette1+74": the base picture, then the overlays.
std::string stack_name(const SceneVariant& stack) {
    std::string name = picture_name(stack.picture, stack.mirrored, stack.ega_palette);
    for (const PictureOverlay& overlay : stack.overlays) {
        name += "+" + picture_name(overlay.picture, overlay.mirrored, overlay.ega_palette);
    }
    return name;
}

std::string stack_hashes(const sci::resource::ResourceManager& manager, const SceneVariant& stack,
                         const std::optional<std::filesystem::path>& hint_directory, sci::core::Executor& executor,
                         sci::platform::AnalysisCache* cache) {
    const std::string prefix = std::format("stack {}", stack_name(stack));
    const auto drawn = sci::platform::draw_variant(manager, stack);
    if (!drawn) {
        std::println(stderr, "{}: {}", prefix, drawn.error().message);
        return prefix + " error\n";
    }
    sci::vector::Scene visual;
    if (cache != nullptr) {
        const auto cached = cache->scene(stack);
        if (!cached) {
            std::println(stderr, "{}: {}", prefix, cached.error().message);
            return prefix + " error\n";
        }
        visual = **cached;
    } else {
        auto analysed = sci::platform::analyse(manager, hint_directory, stack);
        if (!analysed) {
            std::println(stderr, "{}: {}", prefix, analysed.error().message);
            return prefix + " error\n";
        }
        visual = std::move(*analysed);
    }
    return frame_hashes(prefix, visual, drawn->maps, executor);
}

// Analyses the missing entries as the program's first launch does.
void prepare_all(sci::platform::AnalysisCache& cache) {
    const auto start = Clock::now();
    const auto variants = cache.missing(cache.all_variants());
    cache.prepare(variants, [](std::size_t, std::size_t) { return true; });
    std::println(stderr, "prepared {} variants in {} ms", variants.size(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

struct Options {
    bool check = false;
    bool usage = false;
    std::optional<std::filesystem::path> hint_directory;
    std::optional<std::filesystem::path> cache_directory;
    bool prepare = false;
};

Options parse_options(std::span<char*> args) {
    Options options;
    options.usage = args.size() < 3;
    for (std::size_t i = 3; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--check") {
            options.check = true;
        } else if (arg == "--hints" && i + 1 < args.size()) {
            options.hint_directory = std::filesystem::path(args[++i]);
        } else if (arg == "--cache" && i + 1 < args.size()) {
            options.cache_directory = std::filesystem::path(args[++i]);
        } else if (arg == "--prepare") {
            options.prepare = true;
        } else {
            options.usage = true;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    const auto [check, usage, hint_directory, cache_directory, prepare] = parse_options(args);
    if (usage) {
        std::println(
            stderr,
            "usage: sci-golden-hashes <game> <file> [--check] [--hints <directory>] [--cache <directory> [--prepare]]");
        return 2;
    }
    sci::tools::install_stderr_log();
    const auto manager = sci::tools::open_game(args[1]);
    if (!manager) {
        return 1;
    }
    sci::platform::ThreadExecutor executor;
    std::optional<sci::platform::AnalysisCache> cache;
    if (cache_directory) {
        cache.emplace(*manager, hint_directory, cache_directory, executor);
    }
    if (cache && prepare) {
        prepare_all(*cache);
    }
    std::string hashes;
    Clock::duration in_cache{};
    const auto pictures = sci::tools::numbers_of(*manager, sci::resource::ResourceType::Pic);
    for (const std::uint16_t number : pictures) {
        hashes += picture_hashes(*manager, number, hint_directory, executor, cache ? &*cache : nullptr, in_cache);
    }
    if (cache && !pictures.empty()) {
        std::println(stderr, "visual scenes from the cache: {} us per picture",
                     std::chrono::duration_cast<std::chrono::microseconds>(in_cache).count() /
                         static_cast<std::int64_t>(pictures.size()));
    }
    for (const SceneVariant& stack : script_stacks()) {
        hashes += stack_hashes(*manager, stack, hint_directory, executor, cache ? &*cache : nullptr);
    }
    const std::filesystem::path path = args[2];
    if (!check) {
        std::ofstream out(path);
        out << hashes;
        return out ? 0 : 1;
    }
    std::ifstream in(path);
    const std::string expected{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::istringstream want(expected);
    std::istringstream got(hashes);
    int differences = 0;
    while (want || got) {
        std::string a;
        std::string b;
        const bool more_wanted = static_cast<bool>(std::getline(want, a));
        const bool more_got = static_cast<bool>(std::getline(got, b));
        if ((more_wanted || more_got) && a != b) {
            std::println("expected: {}\n     got: {}", a, b);
            ++differences;
        }
    }
    std::println("{}", differences == 0 ? "all golden hashes match" : std::format("{} differences", differences));
    return differences == 0 ? 0 : 1;
}
