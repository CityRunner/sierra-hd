// SPDX-License-Identifier: GPL-3.0-or-later

#include "resources.hpp"

#include "faults.hpp"

#include <format>

namespace sci::vm {

const std::vector<std::uint8_t>* Resources::find(ResourceId id) {
    if (const auto cached = cache_.find(id); cached != cache_.end()) {
        return &cached->second;
    }
    if (!manager_->contains(id)) {
        return nullptr;
    }
    auto loaded = manager_->load(id);
    if (!loaded) {
        warn(std::format("Cannot read resource {}.{:03}: {}", static_cast<int>(id.type), id.number,
                         loaded.error().message));
        return nullptr;
    }
    return &cache_.emplace(id, std::move(loaded->data)).first->second;
}

} // namespace sci::vm
