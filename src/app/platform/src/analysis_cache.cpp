// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/analysis_cache.hpp"

#include "sci/core/log.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/platform/picture_hints.hpp"
#include "sci/vector/build_scene.hpp"
#include "sci/vector/hints.hpp"
#include "sci/vector/scene_io.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace sci::platform {

namespace fs = std::filesystem;

namespace {

constexpr std::array<std::uint8_t, 4> entry_magic{'S', 'H', 'D', 'C'};
constexpr std::uint16_t entry_version = 1;
constexpr std::size_t header_size = entry_magic.size() + 2 + 32;
constexpr std::size_t remembered_scenes = 8;

void add_u32(core::Sha256& hash, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value & 0xFFU),
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU),
        static_cast<std::uint8_t>(value >> 24U),
    };
    hash.update(bytes);
}

std::optional<std::vector<std::uint8_t>> read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace

core::Sha256Digest entry_key(const core::Sha256Digest& game, std::span<const std::uint8_t> hint_file,
                             const SceneVariant& variant) {
    core::Sha256 hash;
    hash.update(game);
    add_u32(hash, vector::analyser_version);
    add_u32(hash, vector::scene_format_version);
    add_u32(hash, static_cast<std::uint32_t>(hint_file.size()));
    hash.update(hint_file);
    add_u32(hash, variant.picture);
    add_u32(hash, variant.plane == vector::Plane::Visual ? 0 : 1);
    add_u32(hash, variant.mirrored ? 1 : 0);
    add_u32(hash, variant.ega_palette);
    return hash.finish();
}

std::vector<std::uint8_t> encode_entry(const core::Sha256Digest& key, const vector::Scene& scene) {
    std::vector<std::uint8_t> bytes(entry_magic.begin(), entry_magic.end());
    bytes.push_back(static_cast<std::uint8_t>(entry_version & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>(entry_version >> 8U));
    bytes.insert(bytes.end(), key.begin(), key.end());
    const std::vector<std::uint8_t> body = vector::write_scene(scene);
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}

core::Result<vector::Scene> decode_entry(std::span<const std::uint8_t> bytes, const core::Sha256Digest& key) {
    if (bytes.size() < header_size || !std::equal(entry_magic.begin(), entry_magic.end(), bytes.begin())) {
        return core::make_error(core::ErrorCode::InvalidData, "not an analysis cache entry");
    }
    const auto version =
        static_cast<std::uint16_t>(static_cast<unsigned>(bytes[4]) | (static_cast<unsigned>(bytes[5]) << 8U));
    if (version != entry_version) {
        return core::make_error(core::ErrorCode::InvalidData, "an analysis cache entry of another version");
    }
    if (!std::equal(key.begin(), key.end(), bytes.subspan(6, key.size()).begin())) {
        return core::make_error(core::ErrorCode::InvalidData, "a stale analysis cache entry");
    }
    return vector::read_scene(bytes.subspan(header_size));
}

std::string entry_file_name(const SceneVariant& variant) {
    std::string name =
        std::format("pic.{:03}.{}", variant.picture, variant.plane == vector::Plane::Visual ? "visual" : "priority");
    if (variant.mirrored) {
        name += ".mirrored";
    }
    if (variant.ega_palette != 0) {
        name += std::format(".palette{}", variant.ega_palette);
    }
    return name + ".scene";
}

core::Sha256Digest game_digest(const resource::ResourceManager& manager) {
    core::Sha256 hash;
    for (const resource::ResourceId id : manager.list()) {
        add_u32(hash, static_cast<std::uint32_t>(id.type));
        add_u32(hash, id.number);
        const auto resource = manager.load(id);
        if (!resource) {
            add_u32(hash, 0xFFFFFFFFU); // Unreadable: part of the key as such.
            continue;
        }
        add_u32(hash, static_cast<std::uint32_t>(resource->data.size()));
        hash.update(resource->data);
    }
    return hash.finish();
}

core::Result<vector::Scene> analyse(const resource::ResourceManager& manager, const std::optional<fs::path>& hints,
                                    const SceneVariant& variant) {
    const auto resource =
        manager.load(resource::ResourceId{.type = resource::ResourceType::Pic, .number = variant.picture});
    if (!resource) {
        return std::unexpected(resource.error());
    }
    const auto commands = pic::parse_pic(resource->data);
    if (!commands) {
        return std::unexpected(commands.error());
    }
    pic::PictureMaps maps;
    pic::DrawTrace trace;
    const auto drawn = pic::draw_picture(
        *commands, maps,
        pic::DrawOptions{.mirrored = variant.mirrored, .add_to = false, .ega_palette = variant.ega_palette}, &trace);
    if (!drawn) {
        return std::unexpected(drawn.error());
    }
    const std::array numbers{variant.picture};
    const PictureHintSet hint_set(hints, manager, numbers);
    return vector::build_scene(maps, trace, variant.plane, hint_set.pointers()).scene;
}

AnalysisCache::AnalysisCache(const resource::ResourceManager& manager, std::optional<fs::path> hints,
                             std::optional<fs::path> directory, core::Executor& executor)
    : manager_(&manager), hints_(std::move(hints)), directory_(std::move(directory)), executor_(&executor),
      game_(game_digest(manager)) {
}

core::Sha256Digest AnalysisCache::key(const SceneVariant& variant) const {
    std::vector<std::uint8_t> hint_file;
    if (hints_) {
        if (auto bytes = read_file(*hints_ / vector::hint_file_name(variant.picture))) {
            hint_file = std::move(*bytes);
        }
    }
    return entry_key(game_, hint_file, variant);
}

std::optional<vector::Scene> AnalysisCache::load(const SceneVariant& variant) const {
    if (!directory_) {
        return std::nullopt;
    }
    const auto bytes = read_file(*directory_ / entry_file_name(variant));
    if (!bytes) {
        return std::nullopt;
    }
    auto scene = decode_entry(*bytes, key(variant));
    if (!scene) {
        core::log(core::LogLevel::Info, core::LogCategory::General,
                  std::format("{}: {}; analysed again", entry_file_name(variant), scene.error().message));
        return std::nullopt;
    }
    return std::move(*scene);
}

void AnalysisCache::store(const SceneVariant& variant, const vector::Scene& scene) const {
    if (!directory_) {
        return;
    }
    std::error_code error;
    fs::create_directories(*directory_, error);
    // Written beside, then renamed, so a cut-off write leaves no entry.
    const fs::path path = *directory_ / entry_file_name(variant);
    const fs::path temporary = fs::path(path).concat(".tmp");
    const std::vector<std::uint8_t> bytes = encode_entry(key(variant), scene);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes to the stream.
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            core::log(core::LogLevel::Warning, core::LogCategory::General,
                      std::format("cannot write {}", temporary.string()));
            return;
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        core::log(core::LogLevel::Warning, core::LogCategory::General,
                  std::format("cannot write {}: {}", path.string(), error.message()));
    }
}

void AnalysisCache::remember(const SceneVariant& variant, std::shared_ptr<const vector::Scene> scene) {
    std::erase_if(recent_, [&](const Remembered& r) { return r.variant == variant; });
    recent_.push_front(Remembered{.variant = variant, .scene = std::move(scene)});
    if (recent_.size() > remembered_scenes) {
        recent_.pop_back();
    }
}

core::Result<std::shared_ptr<const vector::Scene>> AnalysisCache::scene(const SceneVariant& variant) {
    if (const auto it = std::ranges::find(recent_, variant, &Remembered::variant); it != recent_.end()) {
        recent_.splice(recent_.begin(), recent_, it);
        return recent_.front().scene;
    }
    std::shared_ptr<const vector::Scene> found;
    if (auto loaded = load(variant)) {
        found = std::make_shared<const vector::Scene>(std::move(*loaded));
    } else {
        auto analysed = analyse(*manager_, hints_, variant);
        if (!analysed) {
            return std::unexpected(analysed.error());
        }
        store(variant, *analysed);
        found = std::make_shared<const vector::Scene>(std::move(*analysed));
    }
    remember(variant, found);
    return found;
}

std::vector<SceneVariant> AnalysisCache::all_variants() const {
    std::vector<SceneVariant> variants;
    for (const resource::ResourceId id : manager_->list()) {
        if (id.type == resource::ResourceType::Pic) {
            variants.push_back(SceneVariant{.picture = id.number, .plane = vector::Plane::Visual});
            variants.push_back(SceneVariant{.picture = id.number, .plane = vector::Plane::Priority});
        }
    }
    return variants;
}

std::vector<SceneVariant> AnalysisCache::missing(std::span<const SceneVariant> variants) const {
    std::vector<SceneVariant> result;
    for (const SceneVariant& variant : variants) {
        if (!load(variant)) {
            result.push_back(variant);
        }
    }
    return result;
}

bool AnalysisCache::prepare(std::span<const SceneVariant> variants,
                            const std::function<bool(std::size_t done, std::size_t total)>& progress) {
    // Batches of a few variants per worker, so that progress shows and a
    // stop is noticed between batches.
    constexpr std::size_t batch_size = 16;
    if (!progress(0, variants.size())) {
        return false;
    }
    for (std::size_t start = 0; start < variants.size(); start += batch_size) {
        const std::size_t count = std::min(batch_size, variants.size() - start);
        std::vector<std::optional<vector::Scene>> scenes(count);
        executor_->run(count, [&](std::size_t i) {
            if (auto analysed = analyse(*manager_, hints_, variants[start + i])) {
                scenes[i] = std::move(*analysed);
            }
        });
        for (std::size_t i = 0; i < count; ++i) {
            if (const std::optional<vector::Scene>& scene = scenes[i]; scene.has_value()) {
                store(variants[start + i], *scene);
            }
        }
        if (!progress(start + count, variants.size()) && start + count < variants.size()) {
            return false;
        }
    }
    return true;
}

} // namespace sci::platform
