// SPDX-License-Identifier: GPL-3.0-or-later
//
// Development tool: runs a game folder in sci-vm's deterministic mode and
// writes the kernel-call trace, in the format of the reference ScummVM's
// (tools/scummvm-patches/0003-deterministic-trace.patch), so the two can be
// compared line by line (tools/vm-trace/compare.py):
//
//   sci-vm-trace <game folder> <trace file> [--replay <file>] [--seed <n>]
//                [--stop-tick <n>] [--stop-calls <n>] [--callers] [--vm-log]
//
// --vm-log prints the interpreter's warnings and debug messages, such as the
// script patches applied.

#include "game_folder.hpp"
#include "sci/core/log.hpp"
#include "sci/vm/interpreter.hpp"

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::optional<std::uint32_t> parse_number(std::string_view text) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

int usage() {
    std::println(stderr, "usage: sci-vm-trace <game folder> <trace file> [--replay <file>] [--seed <n>] "
                         "[--stop-tick <n>] [--stop-calls <n>] [--callers] [--vm-log]");
    return 2;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): argument parsing.
int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() < 3) {
        return usage();
    }
    sci::tools::install_stderr_log();
    sci::vm::RunOptions options;
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
            std::ifstream file{std::filesystem::path(value)};
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
    const sci::vm::RunResult result = sci::vm::run_deterministic(*manager, options);
    trace.close();
    std::println(stderr, "{} kernel calls in {} ticks", result.calls, result.ticks);
    if (result.fault) {
        std::println(stderr, "stopped: {}", *result.fault);
        return 1;
    }
    return trace ? 0 : 1;
}
