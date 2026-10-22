// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finds the kernel functions the game's scripts reference: every callk in
// the code blocks of every script, after the script patches (milestone 4b).

#pragma once

#include "sci/core/error.hpp"
#include "sci/resource/resource_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sci::vm {

struct KernelReference {
    std::string name; // ScummVM's name, without the k; "?" for a number outside the table.
    int number = 0;   // The kernel function number.
    int script = 0;
    std::uint32_t offset = 0; // Of the callk instruction.
};

[[nodiscard]] core::Result<std::vector<KernelReference>> scan_kernel_calls(const resource::ResourceManager& resources);

} // namespace sci::vm
