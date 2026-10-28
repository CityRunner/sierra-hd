#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs every scenario of tests/scenarios/scenarios.txt in both engines
# (tools/vm-trace/run-scenario.sh) and prints one line per scenario: traces,
# screens, maps, actor positions, parser results, window layout and OPL
# register writes.
#
#   tools/vm-trace/run-all-scenarios.sh [--jobs <n>] [output folder]
#
# --jobs runs n scenarios at a time (default 1); the lines come in the order
# the runs finish. Run it inside the toolchain container with the game
# mounted at /game. The output of a scenario that matches is deleted.
# Exits with 1 if any scenario differs.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="${here}/../.."
jobs=1
if [[ "${1:-}" == "--jobs" ]]; then
    jobs="${2:?usage: run-all-scenarios.sh [--jobs <n>] [output folder]}"
    shift 2
fi
out="${1:-${root}/build/scenarios}"

# One line of scenarios.txt: runs it and prints its summary line.
run_one() {
    local file="$1" stop_tick="$2" seed="$3"
    local name="${file%.txt}-${stop_tick}-${seed}"
    local result code
    result="$("${here}/run-scenario.sh" "${root}/tests/scenarios/${file}" "${stop_tick}" "${out}/${name}" "${seed}" 2>&1)"
    code=$?
    local traces screens maps actors parser layout opl
    traces="$(grep -E '^traces match|^first difference at call' <<< "${result}" | head -n 1)"
    screens="$(grep -E '^screens match|screens compared' <<< "${result}" | tail -n 1)"
    maps="$(grep -E '^maps match|maps compared' <<< "${result}" | tail -n 1)"
    actors="$(grep -E '^actors match|^first difference at (tick [0-9]+, call [0-9]+, object|tick [0-9]+, call [0-9]+: the cast|frame)|frames in the reference' <<< "${result}" | head -n 1)"
    parser="$(grep -E '^parser match|^first difference at tick [0-9]+, call [0-9]+ \((parse|said)|lines in the reference' <<< "${result}" | head -n 1)"
    layout="$(grep -E '^layout match|^first difference at tick [0-9]+, call [0-9]+ \([a-z]+\):|lines in the reference' <<< "${result}" | tail -n 1)"
    opl="$(grep -E '^opl match|^first difference at line [0-9]+, timer call|writes in the reference' <<< "${result}" | tail -n 1)"
    if [[ ${code} -eq 0 ]]; then
        echo "ok    ${name}: ${traces}; ${screens}; ${maps}; ${actors}; ${parser}; ${layout}; ${opl:-no opl comparison}"
        rm -rf "${out:?}/${name}"
    else
        echo "FAIL  ${name}: ${traces:-no trace comparison}; ${screens:-no screen comparison};" \
            "${maps:-no map comparison}; ${actors:-no actor comparison}; ${parser:-no parser comparison};" \
            "${layout:-no layout comparison}; ${opl:-no opl comparison}"
        return 1
    fi
}
export -f run_one
export here root out

grep -vE '^[[:space:]]*(#|$)' "${root}/tests/scenarios/scenarios.txt" |
    xargs -P "${jobs}" -L 1 bash -c 'run_one "$@"' _
# xargs exits with 123 when a run failed.
[[ $? -eq 0 ]]
