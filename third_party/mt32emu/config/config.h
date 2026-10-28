/* Hand-written for sierra-hd in place of the config.h that mt32emu's own
 * CMakeLists.txt generates from src/config.h.in (libmt32emu 2.8.3), with the
 * values of a static build that provides the C++ API only and has no
 * version tagging. See ../VENDORED.md.
 *
 * Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2022 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MT32EMU_CONFIG_H
#define MT32EMU_CONFIG_H

#define MT32EMU_VERSION      "2.8.3"
#define MT32EMU_VERSION_MAJOR 2
#define MT32EMU_VERSION_MINOR 8
#define MT32EMU_VERSION_PATCH 3

/* 0: the full-featured C++ API only. */
#define MT32EMU_EXPORTS_TYPE 0

/* A static library: no export attributes. */
#undef MT32EMU_SHARED

/* No version tagging, so no runtime version check. */
#define MT32EMU_WITH_VERSION_TAGGING 0
#undef MT32EMU_RUNTIME_VERSION_CHECK

#endif /* #ifndef MT32EMU_CONFIG_H */
