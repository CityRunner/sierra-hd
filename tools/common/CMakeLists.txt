# SPDX-License-Identifier: GPL-3.0-or-later
add_library(sci_tools_common STATIC game_folder.cpp interface_check.cpp picture.cpp png.cpp)
target_include_directories(sci_tools_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sci_tools_common PUBLIC sci-core sci-resource sci-pic sci-vector sci-render sci-platform)
sci_target_defaults(sci_tools_common)
