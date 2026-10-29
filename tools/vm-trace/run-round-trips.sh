#!/usr/bin/env bash
# The round-trip check of milestone 8 (docs/ASSUMPTIONS.md, "Milestone 8:
# round trips") over the scenarios, in sci-vm alone (the reference is not
# needed): each scenario runs once without and once with round trips at
# five ticks spread over it (1/6 to 5/6 of its stop tick), and the two runs'
# kernel traces, screens and 1x maps, actors, parser, layout and OPL files
# and their state hashes at the five ticks and at the end must be identical;
# each round trip must give the same persistent and total state hash after
# the save is read into a fresh machine as before it was written. When the outputs differ, each tick is run alone to name the round
# trip that makes the difference.
#
#   tools/vm-trace/run-round-trips.sh [<scenario list> [<results folder>]]
#
# Inside the toolchain container with the game at /game. The list defaults to
# tests/scenarios/scenarios.txt ("<replay> <stop tick> <seed>" per line), the
# results folder to build/round-trips. Resumable: one result file per
# scenario, <replay>-<stop>-<seed>.ok or .fail, with a line per round trip
# ("<tick>@<tick reached> call <n> save <bytes> persistent <equal|differ>
# total <equal|differ>") and the comparison; a scenario with a .ok file is
# skipped. SIERRA_HD_VM_TRACE names sci-vm-trace (default the release
# build), SIERRA_HD_JOBS the scenarios run at once (default 4).
set -uo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
list="${1:-${root}/tests/scenarios/scenarios.txt}"
results="${2:-${root}/build/round-trips}"
export ROUND_TRIP_RESULTS="${results}"
export ROUND_TRIP_TRACE="${SIERRA_HD_VM_TRACE:-${root}/build/release/tools/vm-trace/sci-vm-trace}"
export ROUND_TRIP_SCENARIOS="${root}/tests/scenarios"
mkdir -p "${results}"

one() {
    local file=$1 stop=$2 seed=$3
    local name="${file%.txt}-${stop}-${seed}"
    local results="${ROUND_TRIP_RESULTS}"
    if [[ -f "${results}/${name}.ok" ]]; then
        echo "skip  ${name}"
        return
    fi
    rm -f "${results}/${name}.fail"
    local out="${results}/${name}.work"
    rm -rf "${out}"
    mkdir -p "${out}"
    local ticks=() k
    for k in 1 2 3 4 5; do ticks+=($((stop * k / 6))); done
    local list
    list="$(IFS=,; echo "${ticks[*]}")"
    run() {
        local dir=$1
        shift
        mkdir -p "${out}/${dir}"
        "${ROUND_TRIP_TRACE}" /game "${out}/${dir}/trace.txt" --seed "${seed}" --stop-tick "${stop}" \
            --replay "${ROUND_TRIP_SCENARIOS}/${file}" --screens "${out}/${dir}/screens" \
            --actors "${out}/${dir}/actors.txt" --parser "${out}/${dir}/parser.txt" \
            --layout "${out}/${dir}/layout.txt" --opl "${out}/${dir}/opl.txt" --saves "${out}/${dir}/saves" \
            --state-hash "${list},$((stop - 1))" "${out}/${dir}/state.txt" "$@" \
            2> "${out}/${dir}/log.txt"
    }
    # The first output that differs from the run without round trips, or
    # nothing.
    differs() {
        local dir=$1 f
        for f in trace actors parser layout opl state; do
            if ! cmp -s "${out}/base/${f}.txt" "${out}/${dir}/${f}.txt"; then
                local line
                line="$(cmp "${out}/base/${f}.txt" "${out}/${dir}/${f}.txt" 2>&1 | head -1)"
                echo "${f} differs: ${line##*: }"
                return
            fi
        done
        local screen
        screen="$(diff -rq "${out}/base/screens" "${out}/${dir}/screens" 2>&1 | head -1)"
        if [[ -n "${screen}" ]]; then
            echo "screens differ: ${screen}"
        fi
    }
    local summary=() code=0
    run base
    if ! run trips --round-trip "${list}" "${out}/trips/round-trips.txt"; then
        summary+=("run with round trips failed: $(tail -1 "${out}/trips/log.txt")")
        code=1
    fi
    local requested tick call bytes saved restored persistent label total rest
    while read -r requested tick call bytes saved restored persistent label total rest; do
        if [[ "${call}" == component ]]; then
            summary+=("  component ${bytes} differs")
            continue
        fi
        if [[ "${bytes}" == error || "${saved}" == error ]]; then
            summary+=("${requested}@${tick}: error ${restored} ${persistent} ${label} ${total} ${rest}")
            code=1
            continue
        fi
        summary+=("${requested}@${tick} call ${call} save ${bytes} persistent ${persistent} total ${total} hash ${saved:0:16}")
        [[ "${persistent}" == equal && "${total}" == equal ]] || code=1
    done < "${out}/trips/round-trips.txt"
    local difference
    difference="$(differs trips)"
    if [[ -n "${difference}" ]]; then
        code=1
        summary+=("continuation: ${difference}")
        local t
        for t in "${ticks[@]}"; do
            run "alone-${t}" --round-trip "${t}" "${out}/alone-${t}/round-trips.txt"
            summary+=("  round trip at ${t} alone: $(differs "alone-${t}")")
        done
    else
        summary+=("continuation: identical ($(wc -l < "${out}/base/trace.txt") kernel calls, $(find "${out}/base/screens" -name '*.ppm' | wc -l) screens, $(wc -l < "${out}/base/opl.txt") OPL writes)")
    fi
    if [[ ${code} -eq 0 ]]; then
        printf '%s\n' "${summary[@]}" > "${results}/${name}.ok"
        rm -rf "${out}"
        echo "ok    ${name}"
    else
        printf '%s\n' "${summary[@]}" > "${results}/${name}.fail"
        echo "FAIL  ${name}"
    fi
}
export -f one

grep -v '^#' "${list}" | grep -v '^$' | xargs -P "${SIERRA_HD_JOBS:-4}" -L 1 bash -c 'one "$@"' _
