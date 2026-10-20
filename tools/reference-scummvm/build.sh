#!/usr/bin/env bash
# Builds the reference ScummVM (spec section 3.3) from a ScummVM checkout.
#
#   tools/reference-scummvm/build.sh <scummvm checkout> <build directory>
#
# The checkout must be at the pinned revision (tools/reference-scummvm/REVISION,
# also recorded in docs/ASSUMPTIONS.md). The revision is exported into the
# build directory and the patches in tools/scummvm-patches/ are applied there,
# so the checkout itself stays untouched and may be mounted read-only.
#
# Only the SCI engine is built, without scalers, aspect-ratio correction or
# any network, cloud or speech features. MT-32 emulation stays enabled for the
# sound comparisons of milestone 7. Run it inside the toolchain container so
# the reference is built with the same compiler as the project:
#
#   SIERRA_HD_MOUNTS=/path/to/scummvm:/scummvm \
#       docker/run.sh tools/reference-scummvm/build.sh /scummvm build/reference-scummvm
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
source_dir="${1:?usage: build.sh <scummvm checkout> <build directory>}"
build_dir="${2:?usage: build.sh <scummvm checkout> <build directory>}"
revision="$(tr -d '[:space:]' < "${here}/REVISION")"
patch_dir="${here}/../scummvm-patches"

actual="$(git -C "${source_dir}" rev-parse HEAD)"
if [[ "${actual}" != "${revision}" ]]; then
    echo "ScummVM checkout is at ${actual}, expected the pinned revision ${revision}" >&2
    exit 1
fi

tree="${build_dir}/src"
stamp="${build_dir}/exported-revision"
# The tree is exported again whenever the revision or any patch changes.
shopt -s nullglob
patches=("${patch_dir}"/*.patch)
wanted="${revision} $(cat /dev/null "${patches[@]}" | sha256sum | cut -d' ' -f1)"
mkdir -p "${build_dir}"
if [[ ! -f "${stamp}" || "$(cat "${stamp}")" != "${wanted}" ]]; then
    rm -rf "${tree}"
    mkdir -p "${tree}"
    git -C "${source_dir}" archive --format=tar "${revision}" | tar -x -C "${tree}"
    for patch in "${patches[@]}"; do
        echo "applying $(basename "${patch}")"
        git -C "${tree}" apply --verbose "${patch}"
    done
    echo "${wanted}" > "${stamp}"
fi

cd "${tree}"
if [[ ! -f config.mk ]]; then
    CXX="${CXX:-clang++}" ./configure \
        --disable-all-engines --enable-engine=sci \
        --disable-scalers --disable-hq-scalers --disable-edge-scalers --disable-aspect \
        --disable-cloud --disable-libcurl --disable-sdlnet --disable-discord \
        --disable-tts --disable-fluidsynth --disable-fluidlite \
        --enable-debug --enable-optimizations
fi
make -j"$(nproc)"
echo "reference ScummVM: ${tree}/scummvm"
