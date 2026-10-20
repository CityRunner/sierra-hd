# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_unit_test(NAME sci-core-tests
    SOURCES
        core/byte_reader_test.cpp
        core/error_test.cpp
        core/fp_profile_test.cpp
    DEPENDS sci-core)
