#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The interface check of milestone 6b (sci-hd-frames --check-interface) on
# the scenarios of tests/scenarios/scenarios.txt: every frame whose
# interface changed, in original and HD mode, at every N of the range.
# Prints one line per scenario; exits with 1 if any check failed. Each
# scenario's line is kept in build/interface-check/<scenario>-<N range>.ok
# when it passes, and a later run prints it again instead of checking the
# scenario again (delete the files to check everything anew).
#
#   tools/hd-frames/check-interface.sh [--jobs <n>] [--scales <from>-<to>] [scenario file...]
#
# Without scenario files, every run of scenarios.txt is checked (each
# replay once, at its longest stop tick). Run it inside the toolchain
# container with the game mounted at /game and a release build:
#
#   SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh tools/hd-frames/check-interface.sh --jobs 4
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="${here}/../.."
jobs=1
scales="1-16"
while [[ $# -gt 0 && "$1" == --* ]]; do
    case "$1" in
    --jobs) jobs="$2"; shift 2 ;;
    --scales) scales="$2"; shift 2 ;;
    *) echo "usage: check-interface.sh [--jobs <n>] [--scales <from>-<to>] [scenario file...]" >&2; exit 2 ;;
    esac
done
frames="${SIERRA_HD_HD_FRAMES:-${root}/build/release/tools/hd-frames/sci-hd-frames}"
cache="${SIERRA_HD_ANALYSIS_CACHE:-${root}/build/analysis-cache}"

# A replay and its stop tick: runs the check, prints its summary line.
check_one() {
    local file="$1" stop_tick="$2" seed="$3" result
    local done_file="${root}/build/interface-check/${file%.txt}-${stop_tick}-${seed}-${scales}.ok"
    if [[ -f "${done_file}" ]]; then
        cat "${done_file}"
        return 0
    fi
    result="$("${frames}" /game "${root}/build/interface-check" --check-interface "${scales}" \
        --stop-tick "${stop_tick}" --seed "${seed}" --replay "${root}/tests/scenarios/${file}" \
        --hints "${root}/hints" --cache "${cache}" 2>&1)"
    local code=$?
    local summary
    summary="$(grep -E '^interface check:' <<< "${result}")"
    if [[ ${code} -eq 0 ]]; then
        echo "ok    ${file%.txt}-${stop_tick}-${seed}: ${summary}" | tee "${done_file}"
    else
        echo "FAIL  ${file%.txt}-${stop_tick}-${seed}: ${summary:-no result}"
        grep -E '^tick [0-9]+, N = |stopped' <<< "${result}" | head -n 5 | sed 's/^/      /'
        return 1
    fi
}
export -f check_one
export frames cache root scales

mkdir -p "${root}/build/interface-check"
if [[ $# -gt 0 ]]; then
    lines="$(for file in "$@"; do grep -E "^$(basename "${file}") " "${root}/tests/scenarios/scenarios.txt" | sort -k2,2n | tail -n 1; done)"
else
    # Each replay once, with the longest of its stop ticks (and its seed).
    lines="$(grep -vE '^[[:space:]]*(#|$)' "${root}/tests/scenarios/scenarios.txt" | sort -k1,1 -k2,2n |
        awk '{last[$1] = $0} END {for (f in last) print last[f]}' | sort)"
fi
xargs -P "${jobs}" -L 1 bash -c 'check_one "$@"' _ <<< "${lines}"
[[ $? -eq 0 ]]
