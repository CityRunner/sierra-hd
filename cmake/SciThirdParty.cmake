# SPDX-License-Identifier: GPL-3.0-or-later
#
# Vendored third-party components (spec section 2), built as static
# libraries. Only targets that may use them link them (spec section 4.2):
# SDL3 is for app (window, input, presentation) and, from milestone 7, the
# audio output of sci-audio.

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/SDL3 ${PROJECT_BINARY_DIR}/third_party/SDL3 EXCLUDE_FROM_ALL SYSTEM)
