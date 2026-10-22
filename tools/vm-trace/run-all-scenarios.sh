#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs every scenario of tests/scenarios/scenarios.txt in both engines
# (tools/vm-trace/run-scenario.sh) and prints one line per scenario.
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
    traces="$(grep -E '^traces match|^first difference' <<< "${result}" | head -n 1)"
    screens="$(grep -E '^screens match|screens compared' <<< "${result}" | tail -n 1)"
    if [[ ${code} -eq 0 ]]; then
        echo "ok    ${name}: ${traces}; ${screens}"
        rm -rf "${out:?}/${name}"
    else
        echo "FAIL  ${name}: ${traces:-no trace comparison}; ${screens:-no screen comparison}"
        status=1
    fi
done < "${root}/tests/scenarios/scenarios.txt"
exit "${status}"
