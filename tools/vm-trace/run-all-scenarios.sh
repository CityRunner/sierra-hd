#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs every scenario of tests/scenarios/scenarios.txt in both engines
# (tools/vm-trace/run-scenario.sh) and prints one line per scenario: traces,
# screens, maps, actor positions and parser results.
#
#   tools/vm-trace/run-all-scenarios.sh [output folder]
#
# Run it inside the toolchain container with the game mounted at /game.
# Exits with 1 if any scenario differs.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="${here}/../.."
out="${1:-${root}/build/scenarios}"
status=0
while read -r file stop_tick seed; do
    [[ -z "${file}" || "${file}" == \#* ]] && continue
    name="${file%.txt}-${stop_tick}-${seed}"
    result="$("${here}/run-scenario.sh" "${root}/tests/scenarios/${file}" "${stop_tick}" "${out}/${name}" "${seed}" 2>&1)"
    code=$?
    traces="$(grep -E '^traces match|^first difference at call' <<< "${result}" | head -n 1)"
    screens="$(grep -E '^screens match|screens compared' <<< "${result}" | tail -n 1)"
    maps="$(grep -E '^maps match|maps compared' <<< "${result}" | tail -n 1)"
    actors="$(grep -E '^actors match|^first difference at (tick [0-9]+, call [0-9]+, object|tick [0-9]+, call [0-9]+: the cast|frame)|frames in the reference' <<< "${result}" | head -n 1)"
    parser="$(grep -E '^parser match|^first difference at tick [0-9]+, call [0-9]+ \((parse|said)|lines in the reference' <<< "${result}" | head -n 1)"
    if [[ ${code} -eq 0 ]]; then
        echo "ok    ${name}: ${traces}; ${screens}; ${maps}; ${actors}; ${parser}"
        rm -rf "${out:?}/${name}"
    else
        echo "FAIL  ${name}: ${traces:-no trace comparison}; ${screens:-no screen comparison};" \
            "${maps:-no map comparison}; ${actors:-no actor comparison}; ${parser:-no parser comparison}"
        status=1
    fi
done < "${root}/tests/scenarios/scenarios.txt"
exit "${status}"
