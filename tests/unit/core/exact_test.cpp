// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/exact.hpp"
#include "sci/core/rounding.hpp"
#include "sci_test.hpp"

using sci::core::IntPoint;
using sci::core::Rational;
using sci::core::RationalPoint;

SCI_TEST(rationals_are_normalised_and_ordered) {
    SCI_CHECK(Rational(2, 4) == Rational(1, 2));
    SCI_CHECK(Rational(3, -6) == Rational(-1, 2));
    SCI_CHECK(Rational(0, -5) == Rational(0));
    SCI_CHECK(Rational(-1, 2).num() == -1 && Rational(-1, 2).den() == 2);
    SCI_CHECK(Rational(1, 3) < Rational(1, 2) && Rational(-1, 2) < Rational(-1, 3));
    SCI_CHECK(Rational(1, 3) + Rational(1, 6) == Rational(1, 2));
    SCI_CHECK(Rational(1, 3) - Rational(1, 2) == Rational(-1, 6));
    SCI_CHECK(Rational(2, 3) * Rational(3, 4) == Rational(1, 2));
    SCI_CHECK(Rational(1, 2) / Rational(-1, 4) == Rational(-2));
    SCI_CHECK(Rational(1, 4).to_double() == 0.25);
}

SCI_TEST(rational_points_are_normalised_and_ordered) {
    SCI_CHECK(RationalPoint(2, 4, 2) == RationalPoint(IntPoint{.x = 1, .y = 2}));
    SCI_CHECK(RationalPoint(-1, 1, -2) == RationalPoint(1, -1, 2));
    SCI_CHECK(RationalPoint(1, 0, 3).is_integer() == false);
    SCI_CHECK(RationalPoint(1, 5, 3) < RationalPoint(1, 7, 3));          // Same x, smaller y.
    SCI_CHECK(RationalPoint(1, 9, 3) < RationalPoint(IntPoint{.x = 1})); // Smaller x.
    SCI_CHECK(RationalPoint(1, 9, 3).y() == Rational(3));
}

SCI_TEST(orientation_is_exact) {
    const IntPoint a{.x = 0, .y = 0};
    const IntPoint b{.x = 4, .y = 2};
    SCI_CHECK(sci::core::orientation(a, b, IntPoint{.x = 2, .y = 1}) == 0);
    SCI_CHECK(sci::core::orientation(a, b, IntPoint{.x = 2, .y = 2}) == 1);
    SCI_CHECK(sci::core::orientation(a, b, IntPoint{.x = 2, .y = 0}) == -1);
    // Rational points just off the line on either side.
    SCI_CHECK(sci::core::orientation(a, b, RationalPoint(6, 3, 3)) == 0);
    SCI_CHECK(sci::core::orientation(a, b, RationalPoint(600, 301, 300)) == 1);
    SCI_CHECK(sci::core::orientation(a, b, RationalPoint(600, 299, 300)) == -1);
    SCI_CHECK(sci::core::cross(IntPoint{.x = 1}, IntPoint{.y = 1}) == 1);
    SCI_CHECK(sci::core::dot(IntPoint{.x = 2, .y = 3}, IntPoint{.x = 4, .y = -1}) == 5);
}

SCI_TEST(rounding_helpers) {
    SCI_CHECK(sci::core::floor_div(7, 2) == 3 && sci::core::floor_div(-7, 2) == -4 &&
              sci::core::floor_div(-8, 2) == -4);
    SCI_CHECK(sci::core::ceil_div(7, 2) == 4 && sci::core::ceil_div(-7, 2) == -3 && sci::core::ceil_div(8, 2) == 4);
    SCI_CHECK(sci::core::div_round_half_up(5, 2) == 3 && sci::core::div_round_half_up(4, 3) == 1);
    SCI_CHECK(sci::core::div_round_half_up(3, 2) == 2 && sci::core::div_round_half_up(1, 4) == 0);
    SCI_CHECK(sci::core::round_half_up(2.5) == 3 && sci::core::round_half_up(-2.5) == -2);
    SCI_CHECK(sci::core::round_half_up(2.4999) == 2 && sci::core::round_half_up(-0.5) == 0);
    SCI_CHECK(sci::core::floor_to_int(-0.5) == -1 && sci::core::ceil_to_int(-0.5) == 0);
    SCI_CHECK(sci::core::floor_to_int(3.0) == 3 && sci::core::ceil_to_int(3.25) == 4);
}
