# SPDX-License-Identifier: GPL-3.0-or-later
add_subdirectory(support)
add_subdirectory(unit)
add_subdirectory(fuzz)

# Golden hashes of synthetic scenes (spec section 11).
sci_add_unit_test(NAME sci-golden-tests
    SOURCES golden/synthetic_test.cpp
    DEPENDS sci-pic sci-vector sci-render)
target_include_directories(sci-golden-tests PRIVATE unit/pic unit/render)

# The code that runs on worker threads (label "threads": the TSan job of
# work-in-progress CI runs only these).
sci_add_unit_test(NAME sci-thread-tests
    SOURCES threads/threads_test.cpp
    DEPENDS sci-pic sci-vector sci-render sci-platform
    LABELS threads)
target_include_directories(sci-thread-tests PRIVATE unit/pic)
