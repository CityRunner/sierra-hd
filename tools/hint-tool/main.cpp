// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tooling for the scene-analysis hints (spec section 6.7).
//
//   sci-hint-tool <game> candidates <n> <directory>
//       Proposes the chains of picture n that may be curves the automatic
//       analysis did not fit: runs of three or more connected lines that
//       turn, not all fitted already. Each candidate is tried as an arc, a
//       spline and a straight line under the hint rules, and the kinds the
//       vector path would accept are listed. Writes pic.NNN.candidates.json
//       and pic.NNN.candidates.png (the picture, dimmed, with the accepted
//       candidates numbered) into the directory. The image is derived from
//       the game art: write it to a git-ignored place only.
//   sci-hint-tool <game> check <hint directory>
//       Checks every hint file against the game: it parses, matches its
//       picture's data, and refers to lines the picture draws. Prints what
//       the vector path does with each file; exits with 1 on a problem.
//
// The candidates are suggestions; which of them are curves, and of what
// kind, is decided by looking at the picture (docs/hints.md).

#include "game_folder.hpp"
#include "picture.hpp"
#include "png.hpp"
#include "sci/render/original.hpp"
#include "sci/vector/build_scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sci::tools::Image;
using sci::tools::Rgb;

// Runs break where they turn by more than 75 degrees: such corners are
// drawn, not curves.
constexpr double max_turn_cosine = 0.259;

// Runs that never turn by more than 8 degrees are straight already.
constexpr double straight_cosine = 0.990;

constexpr int image_scale = 3;

struct Candidate {
    std::string id;
    std::vector<std::uint32_t> commands;
    std::vector<std::size_t> primitives; // Of the trace, per command.
    std::vector<sci::pic::Point> points; // From the first line's start, in picture coordinates.
    std::vector<std::string_view> kinds; // Accepted under the hint rules.
};

double turn_cosine(sci::pic::Point a, sci::pic::Point b, sci::pic::Point c) {
    const double x1 = b.x - a.x;
    const double y1 = b.y - a.y;
    const double x2 = c.x - b.x;
    const double y2 = c.y - b.y;
    const double lengths = std::sqrt(((x1 * x1) + (y1 * y1)) * ((x2 * x2) + (y2 * y2)));
    return lengths == 0 ? -1 : ((x1 * x2) + (y1 * y2)) / lengths;
}

// The runs of connected visible lines that turn smoothly and are not all
// fitted by the automatic analysis.
std::vector<Candidate> find_candidates(const sci::pic::DrawTrace& trace, const sci::vector::Scene& automatic) {
    std::vector<bool> visible(trace.primitives.size(), false);
    std::vector<bool> fitted(trace.primitives.size(), false);
    for (const auto& layer : automatic.layers) {
        if (const auto* stroke = std::get_if<sci::vector::Stroke>(&layer); stroke != nullptr && stroke->source >= 0) {
            visible[static_cast<std::size_t>(stroke->source)] = true;
            fitted[static_cast<std::size_t>(stroke->source)] = stroke->fitted;
        }
    }
    std::vector<Candidate> candidates;
    Candidate run;
    std::vector<double> turns;
    const auto finish = [&] {
        const bool turning = std::ranges::any_of(turns, [](double c) { return c < straight_cosine; });
        const bool all_fitted =
            std::ranges::all_of(run.primitives, [&](std::size_t i) { return static_cast<bool>(fitted[i]); });
        if (run.commands.size() >= 3 && turning && !all_fitted) {
            run.id = std::format("c{}", candidates.size() + 1);
            candidates.push_back(run);
        }
        run = {};
        turns.clear();
    };
    for (std::size_t i = 0; i < trace.primitives.size(); ++i) {
        const auto& primitive = trace.primitives[i];
        const auto* line = std::get_if<sci::pic::Line>(&primitive.shape);
        if (line == nullptr || !visible[i] || line->from == line->to ||
            (primitive.state.planes & sci::pic::plane_visual) == 0) {
            finish();
            continue;
        }
        if (!run.points.empty() &&
            (run.points.back() != line->from || trace.primitives[i - 1].picture != primitive.picture)) {
            finish();
        }
        if (run.points.size() >= 2) {
            const double c = turn_cosine(run.points[run.points.size() - 2], run.points.back(), line->to);
            if (c < max_turn_cosine) {
                const sci::pic::Point corner = run.points.back();
                finish();
                run.points.push_back(corner);
            } else {
                turns.push_back(c);
            }
        }
        if (run.points.empty()) {
            run.points.push_back(line->from);
        }
        run.commands.push_back(primitive.command);
        run.primitives.push_back(i);
        run.points.push_back(line->to);
    }
    finish();
    return candidates;
}

// Tries every candidate as each kind, all at once (the candidates share no
// lines), and records the kinds the vector path accepts.
void try_kinds(std::vector<Candidate>& candidates, const sci::tools::DrawnPicture& drawn, int number,
               const sci::core::Sha256Digest& digest) {
    constexpr std::array kinds{
        std::pair{std::string_view("arc"), sci::vector::HintPrimitive::Arc},
        std::pair{std::string_view("spline"), sci::vector::HintPrimitive::Spline},
        std::pair{std::string_view("line"), sci::vector::HintPrimitive::Line},
    };
    for (const auto& [name, primitive] : kinds) {
        sci::vector::PictureHints hints;
        hints.picture = number;
        hints.pic_sha256 = digest;
        for (const Candidate& candidate : candidates) {
            hints.chains.push_back(sci::vector::ChainHint{
                .id = candidate.id,
                .lines = candidate.commands,
                .primitive = primitive,
                .label = {},
                .uncertain = false,
            });
        }
        const std::array<const sci::vector::PictureHints*, 1> per_picture{&hints};
        const auto built = sci::vector::build_scene(drawn.maps, drawn.trace, sci::vector::Plane::Visual, per_picture);
        std::set<std::string> rejected;
        for (const auto& hint : built.stats.rejected_hints) {
            rejected.insert(hint.id);
        }
        for (Candidate& candidate : candidates) {
            if (!rejected.contains(candidate.id)) {
                candidate.kinds.push_back(name);
            }
        }
    }
}

// 3 x 5 digits, one row per byte, bit 2 leftmost.
constexpr std::array<std::array<std::uint8_t, 5>, 10> digits{
    {
        {7, 5, 5, 5, 7},
        {2, 6, 2, 2, 7},
        {7, 1, 7, 4, 7},
        {7, 1, 7, 1, 7},
        {5, 5, 7, 1, 1},
        {7, 4, 7, 1, 7},
        {7, 4, 7, 5, 7},
        {7, 1, 1, 1, 1},
        {7, 5, 7, 5, 7},
        {7, 5, 7, 1, 7},
    },
};

void put(Image& image, int x, int y, Rgb color) {
    if (x >= 0 && y >= 0 && x < image.width() && y < image.height()) {
        image.set(x, y, color);
    }
}

void draw_label(Image& image, int x, int y, const std::string& text) {
    constexpr int size = 2;
    const int width = (static_cast<int>(text.size()) * 4 * size) + size;
    for (int dy = 0; dy < (5 * size) + (2 * size); ++dy) {
        for (int dx = 0; dx < width; ++dx) {
            put(image, x + dx, y + dy, {.r = 0, .g = 0, .b = 0});
        }
    }
    for (std::size_t k = 0; k < text.size(); ++k) {
        if (text[k] < '0' || text[k] > '9') {
            continue;
        }
        const auto& glyph = digits[static_cast<std::size_t>(text[k] - '0')];
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((glyph[static_cast<std::size_t>(row)] & (4U >> static_cast<unsigned>(column))) == 0) {
                    continue;
                }
                for (int sy = 0; sy < size; ++sy) {
                    for (int sx = 0; sx < size; ++sx) {
                        put(image, x + size + (static_cast<int>(k) * 4 * size) + (column * size) + sx,
                            y + size + (row * size) + sy, {.r = 255, .g = 255, .b = 255});
                    }
                }
            }
        }
    }
}

void draw_segment(Image& image, sci::pic::Point a, sci::pic::Point b, Rgb color) {
    const double ax = (a.x + 0.5) * image_scale;
    const double ay = (a.y + 0.5) * image_scale;
    const double bx = (b.x + 0.5) * image_scale;
    const double by = (b.y + 0.5) * image_scale;
    const int steps = static_cast<int>(std::max(std::abs(bx - ax), std::abs(by - ay))) + 1;
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const int x = static_cast<int>(ax + ((bx - ax) * t));
        const int y = static_cast<int>(ay + ((by - ay) * t));
        put(image, x, y, color);
        put(image, x + 1, y, color);
        put(image, x, y + 1, color);
    }
}

Image candidate_image(const sci::tools::DrawnPicture& drawn, const std::vector<Candidate>& candidates) {
    const auto original = sci::render::indices_to_rgba(sci::render::render_original(drawn.maps, image_scale),
                                                       sci::vector::picture_width * image_scale,
                                                       sci::vector::picture_height * image_scale);
    Image image = sci::tools::to_image(original);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const Rgb c = image.at(x, y);
            const auto dim = [](std::uint8_t v) { return static_cast<std::uint8_t>(64 + (v / 3)); };
            image.set(x, y, {.r = dim(c.r), .g = dim(c.g), .b = dim(c.b)});
        }
    }
    constexpr std::array colors{
        Rgb{.r = 255, .g = 64, .b = 64}, Rgb{.r = 64, .g = 255, .b = 64}, Rgb{.r = 80, .g = 160, .b = 255},
        Rgb{.r = 255, .g = 255, .b = 0}, Rgb{.r = 255, .g = 0, .b = 255}, Rgb{.r = 0, .g = 255, .b = 255},
    };
    std::size_t shown = 0;
    for (const Candidate& candidate : candidates) {
        if (candidate.kinds.empty()) {
            continue;
        }
        const Rgb color = colors[shown++ % colors.size()];
        for (std::size_t i = 0; i + 1 < candidate.points.size(); ++i) {
            draw_segment(image, candidate.points[i], candidate.points[i + 1], color);
        }
    }
    for (const Candidate& candidate : candidates) {
        if (!candidate.kinds.empty()) {
            const sci::pic::Point p = candidate.points[candidate.points.size() / 2];
            draw_label(image, (p.x * image_scale) + 4, (p.y * image_scale) + 4, candidate.id.substr(1));
        }
    }
    return image;
}

std::string candidate_json(int number, const std::vector<Candidate>& candidates) {
    std::string text = std::format("{{\n  \"picture\": {},\n  \"candidates\": [", number);
    bool first = true;
    for (const Candidate& candidate : candidates) {
        if (candidate.kinds.empty()) {
            continue;
        }
        std::string commands;
        for (const std::uint32_t command : candidate.commands) {
            commands += std::format("{}{}", commands.empty() ? "" : ", ", command);
        }
        std::string points;
        for (const sci::pic::Point p : candidate.points) {
            points += std::format("{}[{}, {}]", points.empty() ? "" : ", ", p.x, p.y);
        }
        std::string kinds;
        for (const std::string_view kind : candidate.kinds) {
            kinds += std::format("{}\"{}\"", kinds.empty() ? "" : ", ", kind);
        }
        text += std::format("{}\n    {{\"id\": \"{}\", \"lines\": [{}], \"kinds\": [{}], \"points\": [{}]}}",
                            first ? "" : ",", candidate.id, commands, kinds, points);
        first = false;
    }
    text += "\n  ]\n}\n";
    return text;
}

int candidates(const sci::resource::ResourceManager& manager, std::uint16_t number, const fs::path& directory) {
    const std::array numbers{number};
    const auto drawn = sci::tools::draw_pictures(manager, numbers);
    const auto resource =
        manager.load(sci::resource::ResourceId{.type = sci::resource::ResourceType::Pic, .number = number});
    if (!drawn || !resource) {
        return 1;
    }
    const auto automatic = sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Visual);
    std::vector<Candidate> found = find_candidates(drawn->trace, automatic.scene);
    try_kinds(found, *drawn, number, sci::core::sha256(resource->data));
    std::error_code error;
    fs::create_directories(directory, error);
    std::ofstream(directory / std::format("pic.{:03}.candidates.json", number)) << candidate_json(number, found);
    if (!candidate_image(*drawn, found).write_png(directory / std::format("pic.{:03}.candidates.png", number))) {
        std::println(stderr, "cannot write the image");
        return 1;
    }
    const auto accepted = std::ranges::count_if(found, [](const Candidate& c) { return !c.kinds.empty(); });
    std::println("pic {}: {} candidates, {} with an accepted kind; sha256 {}", number, found.size(), accepted,
                 sci::core::to_hex(sci::core::sha256(resource->data)));
    return 0;
}

// Checks one hint file against the game and prints what the vector path
// does with it.
bool check_file(const sci::resource::ResourceManager& manager, const fs::path& path) {
    bool ok = true;
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto hints = sci::vector::parse_hints(text);
    if (!hints) {
        std::println("{}: {}", path.filename().string(), hints.error().message);
        return false;
    }
    if (path.filename().string() != sci::vector::hint_file_name(hints->picture)) {
        std::println("{}: the file is named for another picture", path.filename().string());
        return false;
    }
    const auto number = static_cast<std::uint16_t>(hints->picture);
    const auto resource =
        manager.load(sci::resource::ResourceId{.type = sci::resource::ResourceType::Pic, .number = number});
    if (!resource || !sci::vector::hints_match(*hints, resource->data)) {
        std::println("{}: does not match the picture's data", path.filename().string());
        return false;
    }
    const std::array numbers{number};
    const auto drawn = sci::tools::draw_pictures(manager, numbers);
    if (!drawn) {
        return false;
    }
    const std::array<const sci::vector::PictureHints*, 1> per_picture{&*hints};
    const auto visual = sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Visual, per_picture);
    const auto priority =
        sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Priority, per_picture);
    std::println("{}: {} chains, {} gaps; visual {} fitted, {} closed; priority {} fitted, {} closed",
                 path.filename().string(), hints->chains.size(), hints->gaps.size(), visual.stats.hint_chains,
                 visual.stats.hint_gaps, priority.stats.hint_chains, priority.stats.hint_gaps);
    for (const auto* stats : {&visual.stats, &priority.stats}) {
        for (const auto& rejected : stats->rejected_hints) {
            std::println("  {} plane, {}: {}", stats == &visual.stats ? "visual" : "priority", rejected.id,
                         rejected.detail);
            // A reference to a command without a line is an error in the file.
            ok = ok && !rejected.detail.ends_with("draws no line");
        }
    }
    return ok;
}

int check(const sci::resource::ResourceManager& manager, const fs::path& directory) {
    std::vector<fs::path> files;
    std::error_code error;
    const fs::directory_iterator entries(directory, error);
    if (error) {
        std::println(stderr, "{}: {}", directory.string(), error.message());
        return 1;
    }
    for (const auto& entry : entries) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("pic.") && name.ends_with(".json")) {
            files.push_back(entry.path());
        }
    }
    std::ranges::stable_sort(files);
    bool ok = true;
    for (const fs::path& path : files) {
        ok = check_file(manager, path) && ok;
    }
    return ok ? 0 : 1;
}

std::optional<std::uint16_t> parse_number(std::string_view text) {
    if (text.empty() || text.size() > 3 || !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(std::stoi(std::string(text)));
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    const std::string_view command = args.size() > 2 ? args[2] : "";
    const bool is_check = command == "check" && args.size() == 4;
    const auto number = command == "candidates" && args.size() == 5 ? parse_number(args[3]) : std::nullopt;
    if (!is_check && !number) {
        std::println(stderr, "usage: sci-hint-tool <game> candidates <n> <directory>\n"
                             "       sci-hint-tool <game> check <hint directory>");
        return 2;
    }
    sci::tools::install_stderr_log();
    const auto manager = sci::tools::open_game(args[1]);
    if (!manager) {
        return 1;
    }
    if (number) {
        return candidates(*manager, *number, args[4]);
    }
    return check(*manager, args[3]);
}
