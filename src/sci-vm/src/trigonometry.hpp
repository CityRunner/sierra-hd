// SPDX-License-Identifier: GPL-3.0-or-later
//
// sin and cos of whole degrees, as ScummVM computes them in the kernel
// functions (sin(angle * M_PI / 180.0)), but the same on every platform
// (spec section 5.3): for angles from -360 to 360 the values come from a
// table of the reference platform's results (scripts/gen-trigonometry.py);
// other angles use the C library.

#pragma once

namespace sci::vm {

[[nodiscard]] double sin_degrees(int angle);
[[nodiscard]] double cos_degrees(int angle);

} // namespace sci::vm
