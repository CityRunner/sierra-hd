# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28)

project(sierra-hd
    VERSION 0.1.0
    DESCRIPTION "HD version of The Colonel's Bequest on a custom SCI0 interpreter"
    LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Configure into a separate build directory (see CMakePresets.json).")
endif()

option(SCI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(SCI_BUILD_TESTS "Build the unit and fuzz tests" ON)
option(SCI_BUILD_TOOLS "Build the development tools under tools/" ON)
option(SCI_BUILD_APP "Build the program (sierra-hd) and SDL3; off for the ARM64 tests under qemu-user" ON)
option(SCI_DEBUG_HIGHLIGHT "Development only: let the room viewer highlight vector replacements" OFF)
option(SCI_WITHOUT_STROKE_INTEGRITY
       "Development only: build scenes without the stroke integrity pass (the A1 baseline; golden tests fail)" OFF)

# With ccache as the compiler launcher (CI; CMake takes it from the
# environment), clang-cl writes debug information into the objects (/Z7):
# ccache cannot cache compilations that share a PDB (/Zi, CMake's default).
if(CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

# The project does not use C++ modules; scanning would require clang-scan-deps.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SciCompilerFlags)
include(SciTargets)

# Libraries in dependency order (spec section 4.1); see sci_add_library.
add_subdirectory(src/sci-core)
add_subdirectory(src/sci-resource)
add_subdirectory(src/sci-pic)
add_subdirectory(src/sci-vector)
add_subdirectory(src/sci-view)
add_subdirectory(src/sci-text)
add_subdirectory(src/sci-audio)
add_subdirectory(src/sci-render)
add_subdirectory(src/sci-vm)
if(SCI_BUILD_APP)
    include(SciThirdParty)
endif()
add_subdirectory(src/app)

if(SCI_BUILD_TESTS)
    enable_testing()
    include(SciTesting)
    add_subdirectory(tests)
endif()

if(SCI_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
