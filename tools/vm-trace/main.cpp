// SPDX-License-Identifier: GPL-3.0-or-later
//
// Development tool: runs a game folder in sci-vm's deterministic mode and
// writes the kernel-call trace, in the format of the reference ScummVM's
// (tools/scummvm-patches/0003-deterministic-trace.patch), so the two can be
// compared line by line (tools/vm-trace/compare.py):
//
//   sci-vm-trace <game folder> <trace file> [--replay <file>] [--seed <n>]
//                [--stop-tick <n>] [--stop-calls <n>] [--callers] [--vm-log]
//                [--screens <folder>] [--screen-every <ticks>]
//   sci-vm-trace --scan-kernel <game folder> <output file>
//
// --scan-kernel lists the kernel functions the scripts reference (every
// callk in their code, after the script patches): one line per function,
// "<number> <name> <calls> <scripts>", sorted by number. [--vm-log]
//
// --vm-log prints the interpreter's warnings and debug messages, such as the
// script patches applied.

#include "game_folder.hpp"
#include "sci/core/log.hpp"
#include "sci/render/colors.hpp"
#include "sci/vm/interpreter.hpp"
#include "sci/vm/scan.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
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
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int screen_width = 320;
constexpr int screen_height = 200;

std::optional<std::uint32_t> parse_number(std::string_view text) {
    std::uint32_t value = 0;
    const char* const last = std::to_address(text.end());
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), last, value);
    if (error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

int usage() {
    std::println(stderr, "usage: sci-vm-trace <game folder> <trace file> [--replay <file>] [--seed <n>] "
                         "[--stop-tick <n>] [--stop-calls <n>] [--callers] [--vm-log]\n"
                         "                    [--screens <folder>] [--screen-every <ticks>]");
    return 2;
}

// The screen as a binary PPM (P6), in the EGA colours.
void write_screen(const std::filesystem::path& path, std::span<const std::uint8_t> visual) {
    std::ofstream out{path, std::ios::binary};
    out << std::format("P6\n{} {}\n255\n", screen_width, screen_height);
    for (const std::uint8_t colour : visual) {
        const sci::render::Rgb8 rgb = sci::render::ega_palette.at(colour & 0x0FU);
        out.put(static_cast<char>(rgb.r)).put(static_cast<char>(rgb.g)).put(static_cast<char>(rgb.b));
    }
}

int scan_kernel(const char* folder, const char* output) {
    const auto manager = sci::tools::open_game(folder);
    if (!manager) {
        return 1;
    }
    const auto references = sci::vm::scan_kernel_calls(*manager);
    if (!references) {
        std::println(stderr, "{}", references.error().message);
        return 1;
    }

    struct Usage {
        std::string name;
        int calls = 0;
        std::set<int> scripts;
    };

    std::map<int, Usage> usage;
    for (const sci::vm::KernelReference& reference : *references) {
        Usage& entry = usage[reference.number];
        entry.name = reference.name;
        ++entry.calls;
        entry.scripts.insert(reference.script);
    }
    std::ofstream out{std::filesystem::path(output)};
    for (const auto& [number, entry] : usage) {
        std::string scripts;
        for (const int script : entry.scripts) {
            scripts += std::format("{}{}", scripts.empty() ? "" : ",", script);
        }
        out << std::format("{:#04x} {} {} {}\n", number, entry.name, entry.calls, scripts);
    }
    std::println(stderr, "{} kernel functions referenced", usage.size());
    return out ? 0 : 1;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): argument parsing.
int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() == 4 && std::string_view(args[1]) == "--scan-kernel") {
        sci::tools::install_stderr_log();
        return scan_kernel(args[2], args[3]);
    }
    if (args.size() < 3) {
        return usage();
    }
    sci::tools::install_stderr_log();
    sci::vm::RunOptions options;
    std::filesystem::path screens;
    std::uint32_t screen_every = 60;
    for (std::size_t i = 3; i < args.size(); i += 2) {
        const std::string_view flag = args[i];
        if (flag == "--vm-log") {
            sci::core::set_log_category_enabled(sci::core::LogCategory::Vm, true);
            --i;
            continue;
        }
        if (flag == "--callers") {
            options.trace_callers = true;
            --i;
            continue;
        }
        if (i + 1 >= args.size()) {
            return usage();
        }
        const std::string_view value = args[i + 1];
        if (flag == "--replay") {
            const std::ifstream file{std::filesystem::path(value)};
            std::stringstream text;
            text << file.rdbuf();
            if (!file) {
                std::println(stderr, "cannot read {}", value);
                return 1;
            }
            auto input = sci::vm::parse_replay(text.str());
            if (!input) {
                std::println(stderr, "{}: {}", value, input.error().message);
                return 1;
            }
            options.input = std::move(*input);
            continue;
        }
        if (flag == "--screens") {
            screens = value;
            continue;
        }
        const auto parsed = parse_number(value);
        if (!parsed) {
            return usage();
        }
        if (flag == "--seed") {
            options.seed = *parsed;
        } else if (flag == "--stop-tick") {
            options.stop_tick = *parsed;
        } else if (flag == "--stop-calls") {
            options.stop_calls = *parsed;
        } else if (flag == "--screen-every" && *parsed > 0) {
            screen_every = *parsed;
        } else {
            return usage();
        }
    }
    const auto manager = sci::tools::open_game(args[1]);
    if (!manager) {
        return 1;
    }
    std::ofstream trace{std::filesystem::path(args[2])};
    if (!trace) {
        std::println(stderr, "cannot write {}", args[2]);
        return 1;
    }
    options.trace = [&](std::string_view line) { trace << line << '\n'; };
    std::uint32_t next_screen = 0;
    if (!screens.empty()) {
        std::error_code created;
        std::filesystem::create_directories(screens, created);
        options.on_tick = [&](std::uint32_t tick, std::span<const std::uint8_t> shown) {
            if (tick >= next_screen) {
                write_screen(screens / std::format("{:06}.ppm", tick), shown);
                next_screen = tick - (tick % screen_every) + screen_every;
            }
        };
    }
    // The game folder's files for kFOpen to read, under lower-case names,
    // and nothing written yet: the reference starts with an empty save folder.
    std::map<std::string, std::vector<std::uint8_t>> game_files;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(args[1], error)) {
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
    options.files = &files;
    const sci::vm::RunResult result = sci::vm::run_deterministic(*manager, options);
    trace.close();
    std::println(stderr, "{} kernel calls in {} ticks", result.calls, result.ticks);
    if (result.fault) {
        std::println(stderr, "stopped: {}", *result.fault);
        return 1;
    }
    return trace ? 0 : 1;
}
