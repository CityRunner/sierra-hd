# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-render
    SOURCES
        src/acceptance.cpp
        src/original.cpp
        src/rasterize.cpp
    DEPENDS sci-core sci-pic sci-vector sci-view)
