#!/usr/bin/env bash
# Milestone 2 check: path 1 (pictures), views and fonts are bit-exact with
# ScummVM, compared as palette-index buffers. Runs inside the toolchain
# container with the game folder mounted, for example:
#
#   SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh tools/graphics-compare/run.sh /game
#
# Needs the reference ScummVM built with the patches in tools/scummvm-patches
# (build/reference-scummvm) and a configured build tree (default build/release).
set -euo pipefail
cd "$(dirname "$0")/../.."

game="${1:?usage: run.sh <game folder> [build directory]}"
build="${2:-build/release}"
out="build/dumps"

cmake --build "${build}" --target sci-graphics-dump
rm -rf "${out}/scummvm-gfx" "${out}/sierra-hd-gfx"
mkdir -p "${out}/scummvm-gfx" "${out}/sierra-hd-gfx"

SIERRA_HD_DUMP_GRAPHICS="${PWD}/${out}/scummvm-gfx" SDL_AUDIODRIVER=dummy \
    xvfb-run -a tools/reference-scummvm/run.sh build/reference-scummvm "${game}" > "${out}/scummvm-gfx.log" 2>&1
"${build}/tools/graphics-dump/sci-graphics-dump" "${game}" "${out}/sierra-hd-gfx" 2> "${out}/sierra-hd-gfx.log"

python3 tools/graphics-compare/compare.py "${out}/scummvm-gfx" "${out}/sierra-hd-gfx"
