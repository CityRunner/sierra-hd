// SPDX-License-Identifier: GPL-3.0-or-later

#include "hints.hpp"

#include <fstream>
#include <iterator>
#include <print>
#include <string>
#include <utility>

namespace sci::tools {

PictureHintSet::PictureHintSet(const std::optional<std::filesystem::path>& directory,
                               const resource::ResourceManager& manager, std::span<const std::uint16_t> numbers)
    : hints_(numbers.size()) {
    for (std::size_t i = 0; i < numbers.size() && directory; ++i) {
        const std::filesystem::path path = *directory / vector::hint_file_name(numbers[i]);
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            continue;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto parsed = vector::parse_hints(text);
        if (!parsed) {
            std::println(stderr, "{}: {}; skipped", path.string(), parsed.error().message);
            continue;
        }
        const auto resource =
            manager.load(resource::ResourceId{.type = resource::ResourceType::Pic, .number = numbers[i]});
        if (std::cmp_not_equal(parsed->picture, numbers[i]) || !resource ||
            !vector::hints_match(*parsed, resource->data)) {
            std::println(stderr, "{}: made for other picture data; skipped", path.string());
            continue;
        }
        hints_[i] = std::move(*parsed);
    }
    for (const auto& hints : hints_) {
        pointers_.push_back(hints ? &*hints : nullptr);
    }
}

} // namespace sci::tools
