# SPDX-License-Identifier: GPL-3.0-or-later
#
# Vendored third-party components (spec section 2), built as static
# libraries. Only targets that may use them link them (spec section 4.2):
# SDL3 is for app (window, input, presentation) and, from milestone 7, the
# audio output of sci-audio; Nuked OPL3 is for sci-audio.
#
# Vendored code is not held to the project's warning set (sci_target_defaults
# is not applied), so its warnings never fail the build; the global
# floating-point and sanitizer flags of SciCompilerFlags.cmake still apply.

# --- Nuked OPL3 (milestone 7) ------------------------------------------------
# Built on every platform, including the ARM64 test build without app. Plain
# C; the stereo extension stays off (third_party/nuked-opl3/VENDORED.md), so
# the core has no floating point and no global state.
enable_language(C)
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
        "Vendored C code is built with upstream LLVM Clang too (spec section 2); "
        "found ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}.")
endif()
add_library(sci_nuked_opl3 STATIC ${PROJECT_SOURCE_DIR}/third_party/nuked-opl3/opl3.c)
target_include_directories(sci_nuked_opl3 SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/nuked-opl3)
target_compile_definitions(sci_nuked_opl3 PUBLIC OPL_ENABLE_STEREOEXT=0)
set_target_properties(sci_nuked_opl3 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

# --- SDL3 (milestone 3a) -----------------------------------------------------
# Not in the ARM64 test build (SCI_BUILD_APP=OFF): its sysroot has no ARM64
# window-system libraries.
if(SCI_BUILD_APP)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/SDL3 ${PROJECT_BINARY_DIR}/third_party/SDL3 EXCLUDE_FROM_ALL SYSTEM)
endif()
