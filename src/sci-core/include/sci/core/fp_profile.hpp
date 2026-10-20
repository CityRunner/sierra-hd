// SPDX-License-Identifier: GPL-3.0-or-later
//
// Run-time check of the deterministic floating-point profile (spec section
// 5.3). The compile-time half of the profile (no contraction, no fast-math,
// SSE2 instead of x87) is set by cmake/SciCompilerFlags.cmake; this check
// catches a thread whose environment was changed at run time, for example by
// a library that enables flush-to-zero.

#pragma once

#include "sci/core/error.hpp"

namespace sci::core {

// Checks the calling thread: round-to-nearest, denormal results kept (no
// FTZ), denormal inputs kept (no DAZ), and multiply-add not fused in code
// built with the project flags. Rendering and analysis threads call this
// when they start.
[[nodiscard]] Result<void> check_fp_profile();

} // namespace sci::core
