#!/usr/bin/env bash
# Runs a command inside the toolchain image with the repository mounted at
# /work. Extra read-only mounts (game folder, ScummVM source) are passed with
# SIERRA_HD_MOUNTS="host:container[,host:container...]".
#
#   docker/run.sh cmake --preset linux-debug
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
tag="$("${here}/image-tag.sh")"

args=(--rm --init -v "${repo}:/work" -w /work -e HOME=/tmp -e CCACHE_DISABLE=1)
if [[ -t 0 ]]; then args+=(-it); fi
if [[ -n "${SIERRA_HD_MOUNTS:-}" ]]; then
    IFS=',' read -ra mounts <<< "${SIERRA_HD_MOUNTS}"
    for m in "${mounts[@]}"; do args+=(-v "${m}:ro"); done
fi

docker run "${args[@]}" "${tag}" "$@"
