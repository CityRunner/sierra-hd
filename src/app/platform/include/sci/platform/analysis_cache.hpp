// SPDX-License-Identifier: GPL-3.0-or-later
//
// The vector analysis cache (spec section 10.2): the scenes of every
// picture, analysed on first launch and kept in files next to the
// configuration (docs/formats/analysis-cache.md). An entry is keyed by the
// game data, the analyser version, the picture's hint file and the variant
// drawn; an entry with another key, or one that cannot be read, is analysed
// again and replaced. Missing entries are analysed when first asked for.
#pragma once

#include "sci/core/error.hpp"
#include "sci/core/executor.hpp"
#include "sci/core/sha256.hpp"
#include "sci/resource/resource_manager.hpp"
#include "sci/vector/scene.hpp"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sci::platform {

// A picture drawn over another without clearing it (kDrawPic with add-to),
// with its options.
struct PictureOverlay {
    std::uint16_t picture = 0;
    bool mirrored = false;
    std::uint8_t ega_palette = 0;

    friend auto operator<=>(const PictureOverlay&, const PictureOverlay&) = default;
};

// What the scene of a picture depends on besides the picture itself: the
// plane, the options of kDrawPic that change the drawing, and the overlays
// drawn over it, in order (spec section 6.8: they become layers of the
// scene).
struct SceneVariant {
    std::uint16_t picture = 0;
    vector::Plane plane = vector::Plane::Visual;
    bool mirrored = false;
    std::uint8_t ega_palette = 0;
    // NOLINTNEXTLINE(readability-redundant-member-init): variants without overlays need not name them.
    std::vector<PictureOverlay> overlays = {};

    friend auto operator<=>(const SceneVariant&, const SceneVariant&) = default;
};

// The key of an entry: SHA-256 over the game data's digest, the analyser
// and scene format versions, the hint file's bytes (empty for none) and the
// variant; for each overlay, its options and its hint file
// (`overlay_hint_files`, one per overlay; missing ones count as empty).
[[nodiscard]] core::Sha256Digest entry_key(const core::Sha256Digest& game, std::span<const std::uint8_t> hint_file,
                                           const SceneVariant& variant,
                                           std::span<const std::vector<std::uint8_t>> overlay_hint_files = {});

// An entry file: a header with the key, then the scene (sci/vector/scene_io.hpp).
[[nodiscard]] std::vector<std::uint8_t> encode_entry(const core::Sha256Digest& key, const vector::Scene& scene);
// The scene, or an error when the bytes are not an entry with this key.
[[nodiscard]] core::Result<vector::Scene> decode_entry(std::span<const std::uint8_t> bytes,
                                                       const core::Sha256Digest& key);

// The file name of a variant's entry, for example "pic.044.visual.scene", or
// "pic.174.visual.over.074.scene" with an overlay.
[[nodiscard]] std::string entry_file_name(const SceneVariant& variant);

// SHA-256 over every resource of the game (type, number, size, data), in
// the manager's order: the game files' content as the resources see it.
[[nodiscard]] core::Sha256Digest game_digest(const resource::ResourceManager& manager);

// Analyses one variant as path 1 draws it and the vector path builds it.
[[nodiscard]] core::Result<vector::Scene> analyse(const resource::ResourceManager& manager,
                                                  const std::optional<std::filesystem::path>& hints,
                                                  const SceneVariant& variant);

class AnalysisCache {
public:
    // `hints` is the directory of the hint files, `directory` the cache's
    // (created when first written); with no directory nothing is stored.
    AnalysisCache(const resource::ResourceManager& manager, std::optional<std::filesystem::path> hints,
                  std::optional<std::filesystem::path> directory, core::Executor& executor);

    // The scene: from memory, from its file, or analysed now and stored.
    [[nodiscard]] core::Result<std::shared_ptr<const vector::Scene>> scene(const SceneVariant& variant);

    // The variants the first launch prepares: both planes of every picture,
    // as drawn without options.
    [[nodiscard]] std::vector<SceneVariant> all_variants() const;
    // Those of `variants` without a valid file.
    [[nodiscard]] std::vector<SceneVariant> missing(std::span<const SceneVariant> variants) const;

    // Analyses `variants` on the executor in batches and stores them.
    // `progress(done, total)` is called before the first batch and after
    // each, on the calling thread; when it returns false the rest is left.
    // Returns false if it stopped early.
    bool prepare(std::span<const SceneVariant> variants,
                 const std::function<bool(std::size_t done, std::size_t total)>& progress);

private:
    [[nodiscard]] core::Sha256Digest key(const SceneVariant& variant) const;
    [[nodiscard]] std::optional<vector::Scene> load(const SceneVariant& variant) const;
    void store(const SceneVariant& variant, const vector::Scene& scene) const;
    void remember(const SceneVariant& variant, std::shared_ptr<const vector::Scene> scene);

    const resource::ResourceManager* manager_;
    std::optional<std::filesystem::path> hints_;
    std::optional<std::filesystem::path> directory_;
    core::Executor* executor_;
    core::Sha256Digest game_;

    struct Remembered {
        SceneVariant variant;
        std::shared_ptr<const vector::Scene> scene;
    };

    std::list<Remembered> recent_; // Most recent first.
};

} // namespace sci::platform
