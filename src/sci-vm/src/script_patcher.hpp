// SPDX-License-Identifier: GPL-3.0-or-later
//
// The script patches ScummVM applies to this game when it loads a script
// (engines/sci/engine/script_patches.cpp, spec section 7): byte signatures
// of known script bugs and their replacements.

#pragma once

#include <cstdint>
#include <vector>

namespace sci::vm {

class ScriptPatcher {
public:
    // Applies the patches for script `number` whose signatures match.
    void process(int number, std::vector<std::uint8_t>& buffer) const;
};

} // namespace sci::vm
