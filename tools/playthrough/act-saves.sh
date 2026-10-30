#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Makes a save at the start of each act the playthrough reaches, for the
# customer's play test (milestone 9, docs/playthrough.md). For each line
# "<act number> <tick>" of tests/playthrough/act-saves.txt, the replay runs
# up to that tick, then the game's own save dialog is used as a player would:
# F5, the description ("Act I", "Act II", ...) and Enter. The save is
# written as <output folder>/sierra-hd-<n>.sav, the slot the program would
# give it, so the whole folder can be copied into the program's saves folder.
#
#   tools/playthrough/act-saves.sh [output folder]
#
# Run it inside the toolchain container with the game at /game. The saves
# hold the game's identity: they load only with the same release of the
# game. SIERRA_HD_VM_TRACE names sci-vm-trace (default the release build).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="${here}/../.."
replay="${root}/tests/playthrough/playthrough.txt"
table="${root}/tests/playthrough/act-saves.txt"
romans=(- I II III IV V VI VII VIII)
vm_trace="${SIERRA_HD_VM_TRACE:-${root}/build/release/tools/vm-trace/sci-vm-trace}"
out="${1:-${root}/build/act-saves}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
mkdir -p "${out}"

# A typed character as a replay key event: its ASCII code.
type_text() {
    local tick=$1 text=$2 i
    for ((i = 0; i < ${#text}; i++)); do
        printf '%d key %d\n' $((tick + 10 * i)) "'${text:i:1}"
    done
}

status=0
while read -r number tick _; do
    description="Act ${romans[number]}"
    script="${work}/act-${number}.txt"
    # The replay up to the act's start, then F5 (16128), the description
    # and Enter (13).
    awk -v t="${tick}" '/^[0-9]/ { if ($1 + 0 >= t) next } { print }' "${replay}" > "${script}"
    {
        printf '%d key 16128\n' $((tick + 20))
        type_text $((tick + 200)) "${description}"
        printf '%d key 13\n' $((tick + 200 + 10 * ${#description} + 40))
    } >> "${script}"
    saves="${work}/saves-${number}"
    mkdir -p "${saves}"
    "${vm_trace}" /game "${work}/trace-${number}.txt" --seed 0 --stop-tick $((tick + 600)) --replay "${script}" \
        --saves "${saves}" > "${work}/log-${number}.txt" 2>&1 || true
    save="$(find "${saves}" -name '*.sav' | head -n 1)"
    if [[ -z "${save}" ]]; then
        echo "${description}: no save written (see the dialog at tick ${tick})"
        status=1
        continue
    fi
    cp "${save}" "${out}/$(printf 'sierra-hd-%03d.sav' "${number}")"
    echo "${description}: tick ${tick}, $(printf 'sierra-hd-%03d.sav' "${number}"), $(stat -c %s "${save}") bytes"
done < <(grep -v '^#' "${table}")
exit "${status}"
