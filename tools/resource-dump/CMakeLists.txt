# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(sci-resource-dump main.cpp)
target_link_libraries(sci-resource-dump PRIVATE sci-core sci-resource)
sci_target_defaults(sci-resource-dump)
