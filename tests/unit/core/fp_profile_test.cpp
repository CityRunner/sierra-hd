// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/fp_profile.hpp"
#include "sci_test.hpp"

#include <cmath>
#include <limits>

SCI_TEST(fp_profile_holds_on_the_test_thread) {
    const auto status = sci::core::check_fp_profile();
    SCI_CHECK(status.has_value());
}

// The operations allowed in output-affecting code (spec section 5.3) are
// correctly rounded by IEEE 754, so these results are the same everywhere.
SCI_TEST(allowed_operations_are_exact_ieee) {
    const volatile double two = 2.0;
    SCI_CHECK(std::sqrt(two) == 0x1.6a09e667f3bcdp+0);
    const volatile double third_numerator = 1.0;
    SCI_CHECK(third_numerator / 3.0 == 0x1.5555555555555p-2);
    const volatile double denormal = std::numeric_limits<double>::denorm_min();
    SCI_CHECK(std::floor(denormal) == 0.0);
    SCI_CHECK(std::ceil(denormal) == 1.0);
}
