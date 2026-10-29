// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's resources as the VM reads them: loaded once and kept.

#pragma once

#include "sci/resource/resource_id.hpp"
#include "sci/resource/resource_manager.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace sci::vm {

using resource::ResourceId;
using resource::ResourceType;

class Resources {
public:
    explicit Resources(const resource::ResourceManager& manager) : manager_(&manager) {}

    // The data of a resource, or null if the game has no such resource or
    // it cannot be read.
    [[nodiscard]] const std::vector<std::uint8_t>* find(ResourceId id);

    [[nodiscard]] const std::vector<std::uint8_t>* find(ResourceType type, int number) {
        return find(ResourceId{.type = type, .number = static_cast<std::uint16_t>(number)});
    }

    [[nodiscard]] bool contains(ResourceId id) const { return manager_->contains(id); }

    [[nodiscard]] const resource::ResourceManager& manager() const { return *manager_; }

private:
    const resource::ResourceManager* manager_;
    std::map<ResourceId, std::vector<std::uint8_t>> cache_;
};

} // namespace sci::vm
