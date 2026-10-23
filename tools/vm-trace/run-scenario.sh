#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs one scenario recording in the reference ScummVM and in sci-vm, then
# compares the kernel-call traces, the screens, the 1x priority and control
# maps, the actor positions of every kAnimate call, the parser results of
# every kParse and kSaid call and the window layout (windows, text,
# controls, menus and the cursor in 1x).
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
# sci-vm-layout.txt) and the screens and maps in
# reference/ and sci-vm/. Exits with 1 if anything differs.
set -euo pipefail

replay="${1:?usage: run-scenario.sh <replay> <stop tick> <output folder> [seed]}"
stop_tick="${2:?usage: run-scenario.sh <replay> <stop tick> <output folder> [seed]}"
out="${3:?usage: run-scenario.sh <replay> <stop tick> <output folder> [seed]}"
seed="${4:-0}"
here="$(cd "$(dirname "$0")" && pwd)"
root="${here}/../.."
reference_build="${SIERRA_HD_REFERENCE:-${root}/build/reference-scummvm}"
vm_trace="${SIERRA_HD_VM_TRACE:-${root}/build/debug/tools/vm-trace/sci-vm-trace}"

mkdir -p "${out}"
rm -rf "${out}/reference" "${out}/sci-vm"
mkdir -p "${out}/reference" "${out}/sci-vm"

env SIERRA_HD_TRACE="${out}/reference.txt" SIERRA_HD_STOP_TICK="${stop_tick}" \
    SIERRA_HD_SEED="${seed}" SIERRA_HD_REPLAY="${replay}" SIERRA_HD_SCREENS="${out}/reference" \
    SIERRA_HD_ACTORS="${out}/reference-actors.txt" SIERRA_HD_PARSER="${out}/reference-parser.txt" \
    SIERRA_HD_LAYOUT="${out}/reference-layout.txt" \
    xvfb-run -a env SDL_AUDIODRIVER=dummy \
    "${root}/tools/reference-scummvm/run.sh" "${reference_build}" /game --music-driver=adlib \
    > "${out}/reference.log" 2>&1 || true

"${vm_trace}" /game "${out}/sci-vm.txt" --seed "${seed}" --stop-tick "${stop_tick}" \
    --replay "${replay}" --screens "${out}/sci-vm" --actors "${out}/sci-vm-actors.txt" \
    --parser "${out}/sci-vm-parser.txt" --layout "${out}/sci-vm-layout.txt" \
    > "${out}/sci-vm.log" 2>&1 || true

status=0
python3 "${here}/compare.py" "${out}/reference.txt" "${out}/sci-vm.txt" | tail -n 12 || status=1
python3 "${here}/compare-screens.py" "${out}/reference" "${out}/sci-vm" || status=1
python3 "${here}/compare-actors.py" "${out}/reference-actors.txt" "${out}/sci-vm-actors.txt" || status=1
python3 "${here}/compare-parser.py" "${out}/reference-parser.txt" "${out}/sci-vm-parser.txt" || status=1
python3 "${here}/compare-layout.py" "${out}/reference-layout.txt" "${out}/sci-vm-layout.txt" || status=1
exit "${status}"
