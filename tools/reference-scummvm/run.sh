#!/usr/bin/env bash
# Runs the reference ScummVM with an isolated configuration that shows the
# original 1x output: no scaler, no aspect-ratio correction, no filtering and
# no EGA undithering, and the game's own save and restore dialogs (as
# sci-vm runs them) instead of ScummVM's. Settings stay inside the build
# directory, and so do the saves unless SIERRA_HD_SAVEPATH names another
# folder (run-scenario.sh gives each run an empty one).
#
#   tools/reference-scummvm/run.sh <build directory> [<game folder>] [scummvm options...]
#
# Without a game folder the launcher starts, which is enough to check that the
# build runs. Without a display, wrap the call in xvfb-run; without an audio
# device, set SDL_AUDIODRIVER=dummy.
set -euo pipefail

build_dir="$(cd "${1:?usage: run.sh <build directory> [<game folder>] [options...]}" && pwd)"
shift
binary="${build_dir}/src/scummvm"
config="${build_dir}/reference.ini"
saves="${SIERRA_HD_SAVEPATH:-${build_dir}/saves}"
mkdir -p "${saves}"

cat > "${config}" <<INI
[scummvm]
gfx_mode=normal
scaler=normal
scale_factor=1
aspect_ratio=false
filtering=false
fullscreen=false
disable_dithering=false
music_driver=null
confirm_exit=false
originalsaveload=true
savepath=${saves}
INI

if [[ $# -gt 0 && -d "$1" ]]; then
    game_dir="$1"
    shift
    exec "${binary}" --config="${config}" --path="${game_dir}" --auto-detect "$@"
fi
exec "${binary}" --config="${config}" "$@"
