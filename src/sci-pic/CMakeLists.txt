# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-pic
    SOURCES
        src/canvas.cpp
        src/draw_pic.cpp
        src/parse_pic.cpp
        src/patterns.cpp
    DEPENDS sci-core sci-resource)
