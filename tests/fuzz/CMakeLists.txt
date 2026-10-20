# SPDX-License-Identifier: GPL-3.0-or-later
if(NOT SCI_FUZZ)
    add_library(sci_fuzz_replay_main STATIC replay_main.cpp)
    sci_target_defaults(sci_fuzz_replay_main)
endif()

sci_add_fuzz_target(NAME fuzz-byte-reader
    SOURCE fuzz_byte_reader.cpp
    DEPENDS sci-core
    CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/corpus/byte_reader)

# The list of fuzz targets and their seed corpora, for scripts/run-fuzzers.sh.
if(SCI_FUZZ)
    get_property(targets GLOBAL PROPERTY SCI_FUZZ_TARGETS)
    list(JOIN targets "\n" lines)
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/fuzz-targets.txt CONTENT "${lines}\n")
endif()
