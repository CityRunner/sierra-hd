# SPDX-License-Identifier: GPL-3.0-or-later
#
# Development tools. Nothing here is linked into the program.

add_subdirectory(common)
add_subdirectory(golden-hashes)
add_subdirectory(graphics-dump)
add_subdirectory(hd-report)
add_subdirectory(hint-tool)
add_subdirectory(resource-dump)
add_subdirectory(room-viewer)

# The reference ScummVM (tools/reference-scummvm) is built by its own script
# from a ScummVM checkout outside this repository; this target only wraps it,
# so it is available when SCI_SCUMMVM_SOURCE points at that checkout.
set(SCI_SCUMMVM_SOURCE "" CACHE PATH "ScummVM checkout at the pinned revision (docs/ASSUMPTIONS.md)")

if(SCI_SCUMMVM_SOURCE)
    add_custom_target(reference-scummvm
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/reference-scummvm/build.sh
                ${SCI_SCUMMVM_SOURCE} ${CMAKE_BINARY_DIR}/reference-scummvm
        USES_TERMINAL
        COMMENT "Building the reference ScummVM")
endif()
