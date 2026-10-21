// SPDX-License-Identifier: GPL-3.0-or-later
//
// The milestone 3 replacement report (spec sections 6.4 and 12): for every
// picture of a game folder, what the vector analysis replaced (gap
// closures, fitted curves, brushes) and where it fell back to 1x blocks,
// the acceptance check against path 1, and the room-entry time (path 1,
// both scenes and the HD frame and priority map at the chosen scale).
//
//   sci-hd-report <game> <report.md> [--scale <N>] [--images <directory>] [--hints <directory>]
//                 [--changes <file>]
//
// --hints applies the hint files of a directory (spec section 6.7; the
// repository's are in hints/) and reports what each did.
//
// The A1 checks of milestone 3b (spec section 6.10) are reported per
// picture, with every remaining case and the changes of the stroke
// integrity pass counted by evidence. --changes writes every change with
// its evidence to a file (about 24 MB for the whole game).
//
// The report holds numbers only. --images also writes, per picture, the
// original and the HD frame side by side at scale 3; those images are
// derived from the game art and go to git-ignored places only.

#include "game_folder.hpp"
#include "picture.hpp"
#include "png.hpp"
#include "sci/platform/picture_hints.hpp"
#include "sci/platform/thread_executor.hpp"
#include "sci/render/acceptance.hpp"
#include "sci/render/integrity.hpp"
#include "sci/render/original.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sci::vector::SceneStats;

struct Options {
    fs::path game;
    fs::path report;
    int scale = 10;
    std::optional<fs::path> images;
    std::optional<fs::path> hints;
    std::optional<fs::path> changes;
};

struct Row {
    std::uint16_t number = 0;
    SceneStats visual;
    SceneStats priority;
    sci::render::Acceptance acceptance;
    sci::render::Integrity integrity;
    std::vector<std::string> integrity_cases;   // Described with command indices.
    std::vector<std::string> integrity_changes; // The same, with their evidence.
    double entry_ms = 0;
};

std::string_view kind_name(sci::render::IntegrityKind kind) {
    switch (kind) {
    case sci::render::IntegrityKind::Overshoot:
        return "overshoot";
    case sci::render::IntegrityKind::Depth:
        return "depth";
    case sci::render::IntegrityKind::Doubled:
        return "doubled boundary";
    case sci::render::IntegrityKind::Fragment:
        return "fragment";
    case sci::render::IntegrityKind::Break:
        return "break";
    }
    return "?";
}

// The class of an A1 case for the size summary, and its size bucket.
std::pair<std::string, std::size_t> size_class(const sci::render::IntegrityCase& c) {
    const bool joint = c.kind == sci::render::IntegrityKind::Break && c.other >= 0;
    const bool group = c.kind == sci::render::IntegrityKind::Break && c.other < 0;
    std::string name(kind_name(c.kind));
    if (c.kind == sci::render::IntegrityKind::Overshoot) {
        name = "overshoot, other";
        if (c.past_at_1x) {
            name = "overshoot, end pixel past the contour at 1x";
        } else if (c.along_contour) {
            name = "overshoot, under 30 degrees";
        }
    } else if (joint) {
        name = c.meets_another ? "broken joint (gap), end meets another line" : "broken joint (gap), alone";
    } else if (group) {
        return {c.size == 0 ? "hidden group" : "split group",
                std::min<std::size_t>(static_cast<std::size_t>(c.size), 3)};
    }
    const std::array<double, 3> limits = joint ? std::array{0.125, 0.25, 0.5} : std::array{0.0625, 0.25, 1.0};
    return {name, static_cast<std::size_t>(std::ranges::count_if(limits, [&](double l) { return c.size > l; }))};
}

// The A1 cases of a picture, naming primitives by their PIC command, with
// their class and size.
std::vector<std::string> describe_cases(const sci::render::Integrity& integrity, const sci::pic::DrawTrace& trace) {
    const auto command = [&](std::int32_t p) {
        return p < 0 ? std::string("-") : std::format("{}", trace.primitives[static_cast<std::size_t>(p)].command);
    };
    std::vector<std::string> lines;
    lines.reserve(integrity.cases.size());
    for (const auto& c : integrity.cases) {
        std::string size = std::format("{:.2f} px²", c.size);
        if (c.kind == sci::render::IntegrityKind::Break) {
            size = c.other >= 0 ? std::format("gap {:.2f} px", c.size)
                                : std::format("{} pieces", static_cast<int>(c.size));
        }
        lines.push_back(std::format("{} at ({}, {}): command {}{}, {}", size_class(c).first, c.x, c.y,
                                    command(c.primitive),
                                    c.other >= 0 ? std::format(" and {}", command(c.other)) : std::string(), size));
    }
    return lines;
}

std::string_view change_name(sci::vector::IntegrityChangeKind kind) {
    switch (kind) {
    case sci::vector::IntegrityChangeKind::ClosedJoint:
        return "closed joint";
    case sci::vector::IntegrityChangeKind::ClippedEnd:
        return "clipped end";
    case sci::vector::IntegrityChangeKind::MergedRun:
        return "removed run";
    case sci::vector::IntegrityChangeKind::KeptContact:
        return "kept contact";
    }
    return "?";
}

// The changes of the stroke integrity pass, with their evidence.
std::vector<std::string> describe_changes(const SceneStats& stats, const sci::pic::DrawTrace& trace) {
    std::vector<std::string> lines;
    lines.reserve(stats.integrity_changes.size());
    for (const auto& c : stats.integrity_changes) {
        lines.push_back(std::format("{} at ({}, {}): command {} and {}: {}", change_name(c.kind), c.x, c.y,
                                    trace.primitives[static_cast<std::size_t>(c.primitive)].command,
                                    trace.primitives[static_cast<std::size_t>(c.other)].command, c.evidence));
    }
    return lines;
}

std::optional<Options> parse_options(std::span<char*> args) {
    if (args.size() < 3) {
        return std::nullopt;
    }
    Options options{
        .game = args[1],
        .report = args[2],
        .scale = 10,
        .images = std::nullopt,
        .hints = std::nullopt,
        .changes = std::nullopt,
    };
    for (std::size_t i = 3; i + 1 < args.size(); i += 2) {
        const std::string_view name = args[i];
        const std::string_view value = args[i + 1];
        if (name == "--scale" && value.size() <= 2 && !value.empty() &&
            std::ranges::all_of(value, [](char c) { return c >= '0' && c <= '9'; })) {
            options.scale = std::stoi(std::string(value));
        } else if (name == "--images") {
            options.images = fs::path(value);
        } else if (name == "--hints") {
            options.hints = fs::path(value);
        } else if (name == "--changes") {
            options.changes = fs::path(value);
        } else {
            return std::nullopt;
        }
    }
    if ((args.size() - 3) % 2 != 0 || options.scale < 1) {
        return std::nullopt;
    }
    return options;
}

// The original and the HD frame side by side, at scale 3.
bool write_side_by_side(const sci::tools::DrawnPicture& drawn, const sci::vector::Scene& visual,
                        sci::core::Executor& executor, const fs::path& path) {
    constexpr int scale = 3;
    constexpr int gap = 8;
    const auto original =
        sci::render::indices_to_rgba(sci::render::render_original(drawn.maps, scale),
                                     sci::vector::picture_width * scale, sci::vector::picture_height * scale);
    const auto hd = sci::render::render_hd(visual, scale, executor);
    const sci::tools::Image left = sci::tools::to_image(original);
    const sci::tools::Image right = sci::tools::to_image(hd);
    sci::tools::Image image((2 * left.width()) + gap, left.height(), {.r = 40, .g = 40, .b = 40});
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            image.set(x, y, left.at(x, y));
            image.set(left.width() + gap + x, y, right.at(x, y));
        }
    }
    return image.write_png(path);
}

std::optional<Row> analyse(const sci::resource::ResourceManager& manager, std::uint16_t number, const Options& options,
                           sci::core::Executor& executor) {
    const auto start = std::chrono::steady_clock::now();
    const std::array numbers{number};
    const auto drawn = sci::tools::draw_pictures(manager, numbers);
    if (!drawn) {
        return std::nullopt;
    }
    const sci::platform::PictureHintSet hints(options.hints, manager, numbers);
    const auto visual =
        sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Visual, hints.pointers());
    const auto priority =
        sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Priority, hints.pointers());
    const auto frame = sci::render::render_hd(visual.scene, options.scale, executor);
    const auto priority_map = sci::render::render_priority(priority.scene, options.scale, executor);
    const auto end = std::chrono::steady_clock::now();
    Row row{
        .number = number,
        .visual = visual.stats,
        .priority = priority.stats,
        .acceptance = sci::render::check_acceptance(visual.scene, drawn->maps, drawn->trace),
        .integrity = sci::render::check_integrity(visual.scene, drawn->maps, drawn->trace, executor),
        .integrity_cases = {},
        .integrity_changes = {},
        .entry_ms = std::chrono::duration<double, std::milli>(end - start).count(),
    };
    row.integrity_cases = describe_cases(row.integrity, drawn->trace);
    row.integrity_changes = describe_changes(row.visual, drawn->trace);
    (void)frame;
    (void)priority_map;
    if (options.images) {
        const fs::path path = *options.images / std::format("pic.{:03}.png", number);
        if (!write_side_by_side(*drawn, visual.scene, executor, path)) {
            std::println(stderr, "cannot write {}", path.string());
        }
    }
    return row;
}

std::string table_row(const Row& row) {
    const SceneStats& v = row.visual;
    const auto& a = row.acceptance;
    return std::format(
        "| {} | {} / {} | {} / {} / {} | {} | {} / {} | {} | {} / {} / {} / {} ({}) | {} / {} | {} / {} / {} "
        "| {:.3f}% | {} | {} / {} | {} | {:.0f} |\n",
        row.number, v.strokes, v.lines, v.extensions, v.joins, v.perpendiculars, v.faces, v.thin_faces, v.mixed_faces,
        v.fallback_pixels, v.straightened, v.arcs, v.splines, v.ellipses, v.fitted_lines, v.brushes, v.textured,
        v.hint_chains, v.hint_gaps, v.rejected_hints.size(), 100 * a.agreement(), a.swaps, a.untouched_line_pixels,
        a.overlapped_line_pixels, row.priority.mixed_faces, row.entry_ms);
}

std::string fallback_list(const std::vector<Row>& rows) {
    std::string text;
    for (const Row& row : rows) {
        for (const auto& f : row.visual.fallbacks) {
            text += std::format("- pic {}: {} pixels in ({}, {})-({}, {})\n", row.number, f.pixels, f.left, f.top,
                                f.right, f.bottom);
        }
    }
    return text.empty() ? "None.\n" : text;
}

// Hints used or left out, per plane (a hint of lines in both planes may
// be listed for each).
std::string hint_list(const std::vector<Row>& rows, bool applied) {
    std::string text;
    for (const Row& row : rows) {
        for (const auto& [plane, stats] : {std::pair{"visual", &row.visual}, std::pair{"priority", &row.priority}}) {
            for (const auto& hint : applied ? stats->applied_hints : stats->rejected_hints) {
                text += std::format("- pic {}, {} plane, `{}`: {}\n", row.number, plane, hint.id, hint.detail);
            }
        }
    }
    return text.empty() ? "None.\n" : text;
}

std::string report(const std::vector<Row>& rows, int scale) {
    std::string text = "# HD picture pipeline report\n\n";
    text += "Generated by `tools/hd-report`. Columns: strokes / PIC lines; gap closures (extensions / joins / "
            "perpendicular connectors); faces with a pixel centre; fallback faces (thin / mixed); region pixels "
            "drawn from 1x blocks; fitted chains (straight / arcs / splines / ellipses, and the lines they "
            "replace); brushes (solid / textured); hints of the visual plane (chains fitted / gaps closed / left out); "
            "acceptance (agreement outside the edge band, swaps, line "
            "pixels untouched / overlapped); mixed faces of the priority plane; room entry in ms";
    text += std::format(" (N = {}).\n\n", scale);
    text += "| Pic | Strokes | Gaps | Faces | Fallback | Fallback px | Fits | Brushes | Hints | Agreement | Swaps | "
            "Lines | "
            "Priority mixed | Entry ms |\n";
    text += "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const Row& row : rows) {
        text += table_row(row);
    }
    text += "\n## A1 checks (spec section 6.10)\n\n"
            "Columns: contacts / overshoots; depth violations (pixels); lines drawing the same pixels / doubled "
            "boundaries; fragments; owned line groups and joints / breaks (`docs/SPEC_ADDENDA.md`, "
            "`sci/render/integrity.hpp`); changes of the stroke integrity pass (joints closed / ends clipped / "
            "contacts kept / runs removed).\n\n"
            "| Pic | Overshoot | Depth | Doubled | Fragments | Breaks | Pass |\n|---|---|---|---|---|---|---|\n";
    for (const Row& row : rows) {
        const auto& i = row.integrity;
        const auto& v = row.visual;
        text += std::format("| {} | {} / {} | {} | {} / {} | {} | {} + {} / {} | {} / {} / {} / {} |\n", row.number,
                            i.contacts, i.overshoots, i.depth_violations, i.shared_pairs, i.doubled, i.fragments,
                            i.line_groups, i.joints, i.breaks, v.closed_joints, v.clipped_ends, v.kept_contacts,
                            v.merged_runs);
    }
    text += "\n## A1 changes by evidence\n\n"
            "Every change of the stroke integrity pass, counted by kind and evidence (the full list: "
            "`--changes <file>`).\n\n"
            "| Change | Evidence | Count |\n|---|---|---|\n";
    std::map<std::pair<std::string_view, std::string_view>, int> evidence;
    for (const Row& row : rows) {
        for (const auto& c : row.visual.integrity_changes) {
            ++evidence[{change_name(c.kind), c.evidence}];
        }
    }
    for (const auto& [key, count] : evidence) {
        text += std::format("| {} | {} | {} |\n", key.first, key.second, count);
    }
    text += "\n## A1 cases\n\n";
    for (const Row& row : rows) {
        for (const std::string& c : row.integrity_cases) {
            text += std::format("- pic {}: {}\n", row.number, c);
        }
    }
    text += "\n## Fallbacks (mixed faces of the visual plane)\n\n" + fallback_list(rows);
    text += "\n## Hints used\n\n" + hint_list(rows, true);
    text += "\n## Hints left out\n\n" + hint_list(rows, false);
    return text;
}

void print_summary(const std::vector<Row>& rows) {
    double worst = 1;
    int swaps = 0;
    int untouched = 0;
    int failing = 0;
    double slowest = 0;
    for (const Row& row : rows) {
        worst = std::min(worst, row.acceptance.agreement());
        swaps += row.acceptance.swaps;
        untouched += row.acceptance.untouched_line_pixels;
        failing += row.acceptance.passes() ? 0 : 1;
        slowest = std::max(slowest, row.entry_ms);
    }
    std::println("{} pictures: worst agreement {:.3f}%, {} swaps, {} untouched line pixels, {} failing; slowest "
                 "room entry {:.0f} ms",
                 rows.size(), 100 * worst, swaps, untouched, failing, slowest);
    sci::render::Integrity total;
    for (const Row& row : rows) {
        total.contacts += row.integrity.contacts;
        total.overshoots += row.integrity.overshoots;
        total.overshoots_past_at_1x += row.integrity.overshoots_past_at_1x;
        total.overshoots_along += row.integrity.overshoots_along;
        total.depth_violations += row.integrity.depth_violations;
        total.shared_pairs += row.integrity.shared_pairs;
        total.doubled += row.integrity.doubled;
        total.fragments += row.integrity.fragments;
        total.line_groups += row.integrity.line_groups;
        total.joints += row.integrity.joints;
        total.breaks += row.integrity.breaks;
        total.split_groups += row.integrity.split_groups;
        total.hidden_groups += row.integrity.hidden_groups;
        total.broken_joints += row.integrity.broken_joints;
        total.broken_joints_meeting_another += row.integrity.broken_joints_meeting_another;
    }
    std::println("A1: {} overshoots of {} contacts, {} depth pixels, {} doubled of {} shared pairs, {} fragments, "
                 "{} breaks ({} line groups, {} joints)",
                 total.overshoots, total.contacts, total.depth_violations, total.doubled, total.shared_pairs,
                 total.fragments, total.breaks, total.line_groups, total.joints);
    std::println("A1 overshoots: {} where the end pixel lies past the contour at 1x, {} at under 30 degrees",
                 total.overshoots_past_at_1x, total.overshoots_along);
    std::println("A1 breaks: {} groups split, {} groups hidden, {} joints broken ({} where the end meets another "
                 "line it touches)",
                 total.split_groups, total.hidden_groups, total.broken_joints, total.broken_joints_meeting_another);
    int joints = 0;
    int clipped = 0;
    int kept = 0;
    int merged = 0;
    for (const Row& row : rows) {
        joints += row.visual.closed_joints;
        clipped += row.visual.clipped_ends;
        kept += row.visual.kept_contacts;
        merged += row.visual.merged_runs;
    }
    std::println("A1 pass: {} joints closed, {} ends clipped, {} contacts kept, {} runs merged", joints, clipped, kept,
                 merged);
    // Sizes of the cases, in original pixels (area or gap).
    std::map<std::string, std::array<int, 4>> sizes;
    for (const Row& row : rows) {
        for (const auto& c : row.integrity.cases) {
            const auto [name, bucket] = size_class(c);
            ++sizes[name][bucket];
        }
    }
    for (const auto& [name, counts] : sizes) {
        std::println("A1 sizes, {}: {} / {} / {} / {}", name, counts[0], counts[1], counts[2], counts[3]);
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    const auto options = parse_options(args);
    if (!options) {
        std::println(stderr, "usage: sci-hd-report <game> <report.md> [--scale <N>] [--images <directory>] "
                             "[--hints <directory>] [--changes <file>]");
        return 2;
    }
    sci::tools::install_stderr_log();
    const auto manager = sci::tools::open_game(options->game);
    if (!manager) {
        return 1;
    }
    if (options->images) {
        std::error_code error;
        fs::create_directories(*options->images, error);
    }
    sci::platform::ThreadExecutor executor;
    std::vector<Row> rows;
    bool ok = true;
    for (const std::uint16_t number : sci::tools::numbers_of(*manager, sci::resource::ResourceType::Pic)) {
        if (auto row = analyse(*manager, number, *options, executor)) {
            ok = ok && row->acceptance.passes();
            rows.push_back(std::move(*row));
        } else {
            ok = false;
        }
    }
    std::ofstream out(options->report);
    out << report(rows, options->scale);
    if (options->changes) {
        std::ofstream changes(*options->changes);
        for (const Row& row : rows) {
            for (const std::string& c : row.integrity_changes) {
                changes << std::format("pic {}: {}\n", row.number, c);
            }
        }
        ok = ok && static_cast<bool>(changes);
    }
    print_summary(rows);
    return ok && out ? 0 : 1;
}
