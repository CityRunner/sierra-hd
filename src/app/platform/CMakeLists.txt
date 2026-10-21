# SPDX-License-Identifier: GPL-3.0-or-later
#
# The platform part of app (spec section 4.2): threads and files. The
# program and the development tools both use it.
find_package(Threads REQUIRED)
sci_add_library(NAME sci-platform
    SOURCES
        src/config.cpp
        src/game_files.cpp
        src/identify.cpp
        src/picture_hints.cpp
        src/thread_executor.cpp
    DEPENDS sci-core sci-resource sci-vector)
target_link_libraries(sci-platform PUBLIC Threads::Threads)
