#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the playthrough so far (tests/playthrough/playthrough.txt, seed 0) in
# the reference ScummVM and in sci-vm with tools/vm-trace/run-scenario.sh,
# to the last tick of tests/playthrough/checkpoints.txt (or the stop tick
# given), then checks where sci-vm's game is at each checkpoint (act and
# room, from the globals: tools/playthrough/where.py). Removes the output
# folder when everything matches and holds; exits with 1 otherwise.
#
# Run it inside the toolchain container with the game at /game:
#
#   SIERRA_HD_MOUNTS=/path/to/game:/game,/path/to/reference-build:/ref docker/run.sh \
#       env SIERRA_HD_REFERENCE=/ref tools/playthrough/check.sh [<stop tick>] [<output folder>]
#
# SIERRA_HD_VM_TRACE names the sci-vm-trace to run (default the debug build),
# as for run-scenario.sh.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="${here}/../.."
replay="${root}/tests/playthrough/playthrough.txt"
checkpoints="${root}/tests/playthrough/checkpoints.txt"
vm_trace="${SIERRA_HD_VM_TRACE:-${root}/build/debug/tools/vm-trace/sci-vm-trace}"
ticks="$(grep -v '^#' "${checkpoints}" | awk 'NF >= 4 {print $2}' | sort -n | paste -sd, -)"
stop_tick="${1:-${ticks##*,}}"
out="${2:-${root}/build/playthrough}"

status=0
"${root}/tools/vm-trace/run-scenario.sh" "${replay}" "${stop_tick}" "${out}" 0 || status=1
"${vm_trace}" /game "${out}/globals-trace.txt" --seed 0 --stop-tick "${stop_tick}" --replay "${replay}" \
    --globals "${ticks}" "${out}/globals.txt" > "${out}/globals.log" 2>&1 || status=1
python3 "${here}/where.py" "${out}/globals.txt" "${checkpoints}" || status=1
if [[ "${status}" == 0 ]]; then
    rm -rf "${out}"
    echo "playthrough matches to tick ${stop_tick}"
fi
exit "${status}"
