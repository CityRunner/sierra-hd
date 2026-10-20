// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simple reference of the scene rasteriser (spec section 5.1): the value
// at one point, by testing the layers from the top and then walking the
// face edges, without spans or runs. Differential tests compare the
// optimised sampler with it.
#pragma once

#include "sci/vector/scene.hpp"

#include <cstdint>

namespace sci::render {

// The scene's value at p, which lies in the picture area.
[[nodiscard]] std::uint8_t reference_value(const vector::Scene& scene, vector::Vec2 p);

} // namespace sci::render
