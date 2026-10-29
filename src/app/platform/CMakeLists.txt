# SPDX-License-Identifier: GPL-3.0-or-later
#
# The platform part of app (spec section 4.2): threads, files, the analysis
# cache and the composition of the game's frame. The program and the
# development tools both use it.
find_package(Threads REQUIRED)
sci_add_library(NAME sci-platform
    SOURCES
        src/analysis_cache.cpp
        src/config.cpp
        src/game_files.cpp
        src/game_frame.cpp
        src/hd_pictures.cpp
        src/identify.cpp
        src/mt32_roms.cpp
        src/picture_hints.cpp
        src/save_folder.cpp
        src/thread_executor.cpp
    DEPENDS sci-core sci-resource sci-pic sci-vector sci-render sci-vm sci-audio)
target_link_libraries(sci-platform PUBLIC Threads::Threads)
