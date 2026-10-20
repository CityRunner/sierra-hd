#!/usr/bin/env bash
# Milestone 1 check: every resource decodes byte-identically to ScummVM's
# resource loader. Runs inside the toolchain container with the game folder
# mounted, for example:
#
#   SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh tools/resource-compare/run.sh /game
#
# Needs the reference ScummVM built with the patches in tools/scummvm-patches
# (build/reference-scummvm) and a configured build tree (default build/release).
set -euo pipefail
cd "$(dirname "$0")/../.."

game="${1:?usage: run.sh <game folder> [build directory]}"
build="${2:-build/release}"
out="build/dumps"

cmake --build "${build}" --target sci-resource-dump
rm -rf "${out}/scummvm" "${out}/sierra-hd"
mkdir -p "${out}/scummvm" "${out}/sierra-hd"

SIERRA_HD_DUMP_RESOURCES="${PWD}/${out}/scummvm" SDL_AUDIODRIVER=dummy \
    xvfb-run -a tools/reference-scummvm/run.sh build/reference-scummvm "${game}" > "${out}/scummvm.log" 2>&1
"${build}/tools/resource-dump/sci-resource-dump" "${game}" "${out}/sierra-hd" 2> "${out}/sierra-hd.log"

python3 tools/resource-compare/compare.py "${out}/scummvm" "${out}/sierra-hd"
