# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-vm
    SOURCES
        src/component.cpp
        src/events.cpp
        src/gc.cpp
        src/interpreter.cpp
        src/k_lists.cpp
        src/k_math.cpp
        src/k_scripts.cpp
        src/k_strings.cpp
        src/k_stubs.cpp
        src/kernel.cpp
        src/machine.cpp
        src/object.cpp
        src/resources.cpp
        src/script.cpp
        src/script_patcher.cpp
        src/seg_manager.cpp
        src/segments.cpp
        src/selectors.cpp
        src/subsystems.cpp
        src/timing.cpp
        src/trigonometry.cpp
        src/workarounds.cpp
    DEPENDS sci-core sci-resource sci-pic sci-view sci-text)
