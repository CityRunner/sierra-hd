// SPDX-License-Identifier: GPL-3.0-or-later
//
// Development tool: runs a game folder in sci-vm's deterministic mode and
// writes the kernel-call trace, in the format of the reference ScummVM's
// (tools/scummvm-patches/0003-deterministic-trace.patch), so the two can be
// compared line by line (tools/vm-trace/compare.py):
//
//   sci-vm-trace <game folder> <trace file> [--replay <file>] [--seed <n>]
//                [--stop-tick <n>] [--stop-calls <n>] [--callers] [--vm-log]
//                [--screens <folder>] [--screen-every <ticks>] [--actors <file>]
//                [--parser <file>] [--layout <file>] [--opl <file>]
//   sci-vm-trace --scan-kernel <game folder> <output file>
//   sci-vm-trace --vocabulary <game folder> <output file>
//
// --screens writes the screen the player sees and the 1x priority and
// control maps; --actors writes the cast of every kAnimate call; --parser
// what kParse and kSaid made of the input; --layout the windows, text,
// controls, menus and cursor in 1x; --opl plays the music on the AdLib
// driver and writes its OPL register writes, keyed by the music timer call
// (docs/formats/replay-and-trace.md).
//
// --vocabulary lists the parser's vocabulary as sci-text reads it: every
// word of vocab.000 ("word <spelling> <class> <group>", in hex), the
// suffixes of vocab.901 ("suffix <typed> <stored> <class mask> <class>") and
// the synonyms each script sets with kSetSynonyms ("synonym <script>
// <group> <replacement>").
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
#include "sci/resource/resource_id.hpp"
#include "sci/text/parser.hpp"
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
#include <ostream>
#include <print>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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
                         "                    [--screens <folder>] [--screen-every <ticks>] [--actors <file>] "
                         "[--parser <file>] [--layout <file>] [--opl <file>]\n"
                         "       sci-vm-trace --scan-kernel <game folder> <output file>\n"
                         "       sci-vm-trace --vocabulary <game folder> <output file>");
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

// A 1x map (priority or control, values 0 to 15) as a binary PGM (P5).
void write_map(const std::filesystem::path& path, std::span<const std::uint8_t> map) {
    std::ofstream out{path, std::ios::binary};
    out << std::format("P5\n{} {}\n15\n", screen_width, screen_height);
    for (const std::uint8_t value : map) {
        out.put(static_cast<char>(value));
    }
}

// One line per cast entry of a kAnimate call:
// <tick> <call> <object> <x> <y> <z> <priority> <view> <loop> <cel> <signal>
void write_cast(std::ostream& out, std::uint32_t call, std::uint32_t tick, std::span<const sci::vm::CastEntry> cast) {
    for (const sci::vm::CastEntry& entry : cast) {
        out << std::format("{} {} {:x}:{:04x} {} {} {} {} {} {} {} {:04x}\n", tick, call, entry.object.segment,
                           entry.object.offset, entry.x, entry.y, entry.z, entry.priority, entry.view, entry.loop,
                           entry.cel, entry.signal);
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

// The synonyms block of an SCI0 script (block type 3): pairs of the group a
// word has and the group it is replaced with, as kSetSynonyms reads them.
std::vector<std::pair<std::uint16_t, std::uint16_t>> script_synonyms(std::span<const std::uint8_t> script) {
    constexpr std::uint16_t synonyms_block = 3;
    const auto u16 = [&](std::size_t at) {
        return static_cast<std::uint16_t>(static_cast<unsigned>(script[at]) |
                                          (static_cast<unsigned>(script[at + 1]) << 8U));
    };
    std::vector<std::pair<std::uint16_t, std::uint16_t>> synonyms;
    std::size_t at = 0;
    while (at + 4 <= script.size()) {
        const std::uint16_t type = u16(at);
        const std::uint16_t size = u16(at + 2);
        if (type == 0 || size < 4 || at + size > script.size()) {
            break;
        }
        if (type == synonyms_block) {
            for (std::size_t pair = at + 4; pair + 4 <= at + size; pair += 4) {
                synonyms.emplace_back(u16(pair), u16(pair + 2));
            }
        }
        at += size;
    }
    return synonyms;
}

int dump_vocabulary(const char* folder, const char* output) {
    const auto manager = sci::tools::open_game(folder);
    if (!manager) {
        return 1;
    }
    const auto vocab = [&](std::uint16_t number) -> std::optional<std::vector<std::uint8_t>> {
        auto resource =
            manager->load(sci::resource::ResourceId{.type = sci::resource::ResourceType::Vocab, .number = number});
        if (!resource) {
            return std::nullopt;
        }
        return std::move(resource->data);
    };
    const auto words = vocab(0);
    const auto suffixes = vocab(901);
    const auto branches = vocab(900);
    const auto span_of = [](const std::optional<std::vector<std::uint8_t>>& data) {
        return data ? std::optional<std::span<const std::uint8_t>>(*data) : std::nullopt;
    };
    const sci::text::Vocabulary vocabulary(span_of(words), span_of(suffixes), span_of(branches));
    for (const std::string& warning : vocabulary.warnings()) {
        std::println(stderr, "{}", warning);
    }
    std::ofstream out{std::filesystem::path(output)};
    for (const auto& [spelling, meanings] : vocabulary.words()) {
        for (const sci::text::ResultWord& meaning : meanings) {
            out << std::format("word {} {:03x} {:03x}\n", spelling, meaning.word_class, meaning.group);
        }
    }
    for (const sci::text::Suffix& suffix : vocabulary.suffixes()) {
        out << std::format("suffix {} {} {:03x} {:03x}\n", suffix.alt_suffix, suffix.word_suffix, suffix.class_mask,
                           suffix.result_class);
    }
    for (const sci::resource::ResourceId& id : manager->list()) {
        if (id.type != sci::resource::ResourceType::Script) {
            continue;
        }
        const auto script = manager->load(id);
        if (!script) {
            continue;
        }
        for (const auto& [group, replacement] : script_synonyms(script->data)) {
            out << std::format("synonym {} {:03x} {:03x}\n", id.number, group, replacement);
        }
    }
    std::println(stderr, "{} words, {} suffixes", vocabulary.words().size(), vocabulary.suffixes().size());
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
    if (args.size() == 4 && std::string_view(args[1]) == "--vocabulary") {
        sci::tools::install_stderr_log();
        return dump_vocabulary(args[2], args[3]);
    }
    if (args.size() < 3) {
        return usage();
    }
    sci::tools::install_stderr_log();
    sci::vm::RunOptions options;
    std::filesystem::path screens;
    std::filesystem::path actors;
    std::filesystem::path parser;
    std::filesystem::path layout;
    std::filesystem::path opl;
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
        if (flag == "--actors") {
            actors = value;
            continue;
        }
        if (flag == "--parser") {
            parser = value;
            continue;
        }
        if (flag == "--layout") {
            layout = value;
            continue;
        }
        if (flag == "--opl") {
            opl = value;
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
        options.on_tick = [&](const sci::vm::ScreenMaps& maps) {
            const std::uint32_t tick = maps.tick;
            if (tick >= next_screen) {
                write_screen(screens / std::format("{:06}.ppm", tick), maps.shown);
                write_map(screens / std::format("{:06}.priority.pgm", tick), maps.priority);
                write_map(screens / std::format("{:06}.control.pgm", tick), maps.control);
                next_screen = tick - (tick % screen_every) + screen_every;
            }
        };
    }
    std::ofstream actor_file;
    if (!actors.empty()) {
        actor_file.open(actors);
        if (!actor_file) {
            std::println(stderr, "cannot write {}", actors.string());
            return 1;
        }
        options.on_animate = [&](std::uint32_t call, std::uint32_t tick, std::span<const sci::vm::CastEntry> cast) {
            write_cast(actor_file, call, tick, cast);
        };
    }
    std::ofstream parser_file;
    if (!parser.empty()) {
        parser_file.open(parser);
        if (!parser_file) {
            std::println(stderr, "cannot write {}", parser.string());
            return 1;
        }
        options.on_parser = [&](std::uint32_t call, std::uint32_t tick, std::string_view line) {
            parser_file << tick << ' ' << call << ' ' << line << '\n';
        };
    }
    std::ofstream layout_file;
    if (!layout.empty()) {
        layout_file.open(layout);
        if (!layout_file) {
            std::println(stderr, "cannot write {}", layout.string());
            return 1;
        }
        options.on_layout = [&](std::uint32_t call, std::uint32_t tick, std::string_view line) {
            layout_file << tick << ' ' << call << ' ' << line << '\n';
        };
    }
    std::ofstream opl_file;
    if (!opl.empty()) {
        opl_file.open(opl);
        if (!opl_file) {
            std::println(stderr, "cannot write {}", opl.string());
            return 1;
        }
        options.on_opl = [&](std::uint32_t timer_call, std::uint16_t reg, std::uint8_t value) {
            opl_file << std::format("{} {:03x} {:02x}\n", timer_call, reg, value);
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
    actor_file.close();
    parser_file.close();
    layout_file.close();
    opl_file.close();
    std::println(stderr, "{} kernel calls in {} ticks", result.calls, result.ticks);
    if (result.fault) {
        std::println(stderr, "stopped: {}", *result.fault);
        return 1;
    }
    return trace && (actors.empty() || actor_file) && (parser.empty() || parser_file) && (layout.empty() || layout_file) &&
                   (opl.empty() || opl_file)
               ? 0
               : 1;
}
