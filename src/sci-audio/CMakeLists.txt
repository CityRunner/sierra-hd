# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-audio
    SOURCES src/adlib.cpp src/component.cpp src/mixer.cpp src/mt32.cpp src/mt32_driver.cpp src/opl3.cpp
    DEPENDS sci-core sci-resource)
# The OPL core and the MT-32 synthesiser (cmake/SciThirdParty.cmake),
# private: their headers are used only in src/opl3.cpp and src/mt32.cpp.
target_link_libraries(sci-audio PRIVATE sci_nuked_opl3 sci_mt32emu)
