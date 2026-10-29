#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs one scenario recording in the reference ScummVM and in sci-vm, then
# compares the kernel-call traces, the screens, the 1x priority and control
# maps, the actor positions of every kAnimate call, the parser results of
# every kParse and kSaid call, the window layout (windows, text,
# controls, menus and the cursor in 1x) and the OPL register writes of the
# AdLib driver (skipped with SIERRA_HD_COMPARE_OPL=0).
#
# With SIERRA_HD_MUSIC=mt32 both engines play the music on the MT-32 driver
# instead and the MIDI messages it sends are compared in place of the OPL
# writes. The reference needs MT32_CONTROL.ROM and MT32_PCM.ROM (under those
# names) in SIERRA_HD_MT32_SCUMMVM (default /roms/mt32-scummvm), sci-vm-trace
# a ROM folder as the program reads it in SIERRA_HD_MT32_ROMS (default
# /roms/roland; empty: the messages are only logged):
#
#   SIERRA_HD_MOUNTS=/path/to/game:/game,/home/user/roms:/roms docker/run.sh \
#       env SIERRA_HD_MUSIC=mt32 tools/vm-trace/run-scenario.sh tests/scenarios/boot.txt 3600 build/scenarios/boot-mt32
#
#   tools/vm-trace/run-scenario.sh <replay> <stop tick> <output folder> [seed]
#
# Run it inside the toolchain container with the game mounted at /game:
#
#   SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
#       tools/vm-trace/run-scenario.sh tests/scenarios/menu-keys.txt 3600 build/scenarios/menu-keys
#
# The output folder receives the traces (reference.txt, sci-vm.txt), the actor
# positions (reference-actors.txt, sci-vm-actors.txt), the parser results
# (reference-parser.txt, sci-vm-parser.txt), the layout (reference-layout.txt,
# sci-vm-layout.txt), the OPL writes (reference-opl.txt, sci-vm-opl.txt) and
# the MIDI messages (reference-midi.txt, sci-vm-midi.txt) and the screens
# and maps in reference/ and sci-vm/; the saves each engine wrote, starting
# from none, in reference-saves/ and sci-vm-saves/. Exits with 1 if anything
# differs.
set -euo pipefail

replay="${1:?usage: run-scenario.sh <replay> <stop tick> <output folder> [seed]}"
stop_tick="${2:?usage: run-scenario.sh <replay> <stop tick> <output folder> [seed]}"
out="${3:?usage: run-scenario.sh <replay> <stop tick> <output folder> [seed]}"
seed="${4:-0}"
here="$(cd "$(dirname "$0")" && pwd)"
root="${here}/../.."
reference_build="${SIERRA_HD_REFERENCE:-${root}/build/reference-scummvm}"
vm_trace="${SIERRA_HD_VM_TRACE:-${root}/build/debug/tools/vm-trace/sci-vm-trace}"
compare_opl="${SIERRA_HD_COMPARE_OPL:-1}"
music="${SIERRA_HD_MUSIC:-adlib}"
reference_opl=()
sci_vm_opl=()
reference_driver=(--music-driver=adlib)
if [[ "${music}" == mt32 ]]; then
    compare_opl=0
    reference_opl=(SIERRA_HD_MIDI="${out}/reference-midi.txt")
    reference_driver=(--music-driver=mt32 --extrapath="${SIERRA_HD_MT32_SCUMMVM:-/roms/mt32-scummvm}")
    sci_vm_opl=(--music mt32 --midi "${out}/sci-vm-midi.txt")
    sci_vm_roms="${SIERRA_HD_MT32_ROMS-/roms/roland}"
    if [[ -n "${sci_vm_roms}" ]]; then
        sci_vm_opl+=(--roms "${sci_vm_roms}")
    fi
elif [[ "${music}" != adlib ]]; then
    echo "SIERRA_HD_MUSIC must be adlib or mt32" >&2
    exit 2
elif [[ "${compare_opl}" != 0 ]]; then
    reference_opl=(SIERRA_HD_OPL="${out}/reference-opl.txt")
    sci_vm_opl=(--opl "${out}/sci-vm-opl.txt")
fi

mkdir -p "${out}"
rm -rf "${out}/reference" "${out}/sci-vm" "${out}/reference-saves" "${out}/sci-vm-saves"
mkdir -p "${out}/reference" "${out}/sci-vm" "${out}/reference-saves" "${out}/sci-vm-saves"

env SIERRA_HD_TRACE="${out}/reference.txt" SIERRA_HD_STOP_TICK="${stop_tick}" \
    SIERRA_HD_SEED="${seed}" SIERRA_HD_REPLAY="${replay}" SIERRA_HD_SCREENS="${out}/reference" \
    SIERRA_HD_ACTORS="${out}/reference-actors.txt" SIERRA_HD_PARSER="${out}/reference-parser.txt" \
    SIERRA_HD_LAYOUT="${out}/reference-layout.txt" SIERRA_HD_SAVEPATH="${out}/reference-saves" "${reference_opl[@]}" \
    xvfb-run -a env SDL_AUDIODRIVER=dummy \
    "${root}/tools/reference-scummvm/run.sh" "${reference_build}" /game "${reference_driver[@]}" \
    > "${out}/reference.log" 2>&1 || true

"${vm_trace}" /game "${out}/sci-vm.txt" --seed "${seed}" --stop-tick "${stop_tick}" \
    --replay "${replay}" --screens "${out}/sci-vm" --actors "${out}/sci-vm-actors.txt" \
    --parser "${out}/sci-vm-parser.txt" --layout "${out}/sci-vm-layout.txt" --saves "${out}/sci-vm-saves" "${sci_vm_opl[@]}" \
    > "${out}/sci-vm.log" 2>&1 || true

status=0
python3 "${here}/compare.py" "${out}/reference.txt" "${out}/sci-vm.txt" | tail -n 12 || status=1
python3 "${here}/compare-screens.py" "${out}/reference" "${out}/sci-vm" || status=1
python3 "${here}/compare-actors.py" "${out}/reference-actors.txt" "${out}/sci-vm-actors.txt" || status=1
python3 "${here}/compare-parser.py" "${out}/reference-parser.txt" "${out}/sci-vm-parser.txt" || status=1
python3 "${here}/compare-layout.py" "${out}/reference-layout.txt" "${out}/sci-vm-layout.txt" || status=1
if [[ "${compare_opl}" != 0 ]]; then
    python3 "${here}/compare-opl.py" "${out}/reference-opl.txt" "${out}/sci-vm-opl.txt" || status=1
fi
if [[ "${music}" == mt32 ]]; then
    python3 "${here}/compare-midi.py" "${out}/reference-midi.txt" "${out}/sci-vm-midi.txt" || status=1
fi
exit "${status}"
