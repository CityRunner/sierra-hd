# SPDX-License-Identifier: GPL-3.0-or-later
add_library(sci_test_support STATIC sci_test.cpp)
target_include_directories(sci_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
sci_target_defaults(sci_test_support)
