// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/fp_profile.hpp"

#include <cfenv>
#include <cfloat>
#include <limits>

// FLT_EVAL_METHOD 0 means double arithmetic is evaluated in double, which
// rules out x87 extended precision (spec section 5.3: SSE2 on x86-64).
static_assert(FLT_EVAL_METHOD == 0, "double must be evaluated in double precision (no x87)");
static_assert(std::numeric_limits<double>::is_iec559, "double must be IEEE 754 binary64");

namespace sci::core {

namespace {

// Operands are read through volatile, so the checks run on the hardware of
// the calling thread instead of being folded at compile time.
double load(const volatile double& value) noexcept {
    return value;
}

} // namespace

Result<void> check_fp_profile() {
    if (std::fegetround() != FE_TONEAREST) {
        return make_error(ErrorCode::Unsupported, "floating-point rounding mode is not round-to-nearest");
    }

    const volatile double smallest_normal = std::numeric_limits<double>::min();
    if (load(smallest_normal) / 2.0 == 0.0) {
        return make_error(ErrorCode::Unsupported, "denormal results are flushed to zero (FTZ is on)");
    }

    const volatile double smallest_denormal = std::numeric_limits<double>::denorm_min();
    if (load(smallest_denormal) * 2.0 == 0.0) {
        return make_error(ErrorCode::Unsupported, "denormal inputs are treated as zero (DAZ is on)");
    }

    // (1 + 2^-30)^2 = 1 + 2^-29 + 2^-60. Rounded to double, the product loses
    // the 2^-60 term and the difference is 0; a fused multiply-add keeps it.
    const volatile double x = 1.0 + 0x1p-30;
    const volatile double x_squared_rounded = 1.0 + 0x1p-29;
    if ((load(x) * load(x)) - load(x_squared_rounded) != 0.0) {
        return make_error(ErrorCode::Unsupported, "multiply-add was contracted into FMA (-ffp-contract=off missing)");
    }

    return {};
}

} // namespace sci::core
