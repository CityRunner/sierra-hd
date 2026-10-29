#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the playthrough (tests/playthrough/playthrough.txt, seed 0) in the
# program, sierra-hd, headless under Xvfb, from the boot to QUIT on the last
# screen, with a configuration of its own (docs/playthrough.md, "The
# playthrough in the program"), and checks:
#   - the program ends by itself with status 0 when the game quits, within
#     the time limit;
#   - its log has no error or warning line, other than the accepted ones
#     (below);
#   - the game ends in the state of sci-vm-trace's run of the same replay:
#     the same tick, the same globals and the same state hash (--end-state
#     against --globals and --state-hash at a tick past the end).
# Writes <output>/result.txt (one line: name, status, wall seconds, log
# counts, end-state check) and keeps the log; exits with 1 if a check fails.
#
# Run it inside the toolchain container with the game at /game (and the
# MT-32 ROMs at /roms for --music mt32):
#
#   SIERRA_HD_MOUNTS=/path/to/game:/game,/path/to/roms:/roms docker/run.sh \
#       tools/playthrough/program-run.sh --mode hd --scale 4 --music adlib --out build/program-run/hd4
#
# Options:
#   --mode hd|original     graphics mode (default hd)
#   --scale <N>            N (default 4)
#   --music adlib|mt32|off music device (default adlib)
#   --rom <folder>         mt32_rom (default /roms/roland)
#   --cache warm|cold      cold empties the analysis cache first (default warm)
#   --cache-dir <folder>   the analysis cache shared by the runs
#                          (default build/program-run/cache)
#   --audio dummy|disk     SDL's audio driver (default dummy); disk writes
#                          <output>/audio.raw, removed after the run
#   --real                 real time (about 94 minutes) instead of --fast
#   --limit <seconds>      wall-time limit (default 3600, real time 7200)
#   --out <folder>         output folder (default build/program-run/<mode><N>-<music>)
#
# SIERRA_HD_PROGRAM names the sierra-hd to run (default the release build),
# SIERRA_HD_VM_TRACE the sci-vm-trace for the reference end state (default
# the release build). The reference end state is kept in
# build/program-run/reference-<music>.txt and made when missing.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
replay="${root}/tests/playthrough/playthrough.txt"
program="${SIERRA_HD_PROGRAM:-${root}/build/release/src/app/sierra-hd}"
vm_trace="${SIERRA_HD_VM_TRACE:-${root}/build/release/tools/vm-trace/sci-vm-trace}"
base="${root}/build/program-run"

mode=hd scale=4 music=adlib rom=/roms/roland cache=warm audio=dummy fast=1 limit="" out=""
cache_dir="${base}/cache"
while [[ $# -gt 0 ]]; do
    case "$1" in
    --mode) mode="$2"; shift 2 ;;
    --scale) scale="$2"; shift 2 ;;
    --music) music="$2"; shift 2 ;;
    --rom) rom="$2"; shift 2 ;;
    --cache) cache="$2"; shift 2 ;;
    --cache-dir) cache_dir="$2"; shift 2 ;;
    --audio) audio="$2"; shift 2 ;;
    --real) fast=0; shift ;;
    --limit) limit="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
name="${mode}${scale}-${music}-${cache}$([[ "${fast}" == 1 ]] && echo -fast || echo -real)"
out="${out:-${base}/${name}}"
limit="${limit:-$([[ "${fast}" == 1 ]] && echo 3600 || echo 7200)}"

# The run folder: the program reads sierra-hd.ini, hints/ and cache/ next
# to its executable (src/app/README.md), so it gets a copy of its own.
rm -rf "${out}"
mkdir -p "${out}/run" "${cache_dir}"
cp "${program}" "${out}/run/sierra-hd"
ln -s "${root}/hints" "${out}/run/hints"
if [[ "${cache}" == cold ]]; then
    rm -rf "${cache_dir:?}"/*
fi
ln -s "$(cd "${cache_dir}" && pwd)" "${out}/run/cache"
cat > "${out}/run/sierra-hd.ini" <<EOF
[sierra-hd]
format = 1
game_folder = /game
mode = ${mode}
scale = ${scale}
music = ${music}
mt32_rom = ${rom}
volume = 100
saves = ${out}/saves
EOF

# The reference: sci-vm-trace's end state. The music device does not touch
# the VM state but for the MT-32's polyphony, so MT-32 runs get their own.
reference="${base}/reference-${music}.txt"
if [[ ! -f "${reference}" ]]; then
    device=()
    if [[ "${music}" == mt32 ]]; then
        device=(--music mt32)
    fi
    "${vm_trace}" /game /dev/null --seed 0 --replay "${replay}" "${device[@]}" \
        --globals 4000000000 "${base}/reference-globals.tmp" \
        --state-hash 4000000000 "${base}/reference-hash.tmp" > "${base}/reference.log" 2>&1
    cat "${base}/reference-globals.tmp" "${base}/reference-hash.tmp" > "${reference}"
    rm -f "${base}/reference-globals.tmp" "${base}/reference-hash.tmp"
fi

audio_env=(SDL_AUDIODRIVER="${audio}")
if [[ "${audio}" == disk ]]; then
    audio_env+=(SDL_AUDIO_DISK_OUTPUT_FILE="${out}/audio.raw")
fi
args=(--replay "${replay}" --seed 0 --end-state "${out}/end-state.txt")
if [[ "${fast}" == 1 ]]; then
    args+=(--fast)
fi
start="$(date +%s)"
status=0
timeout "${limit}" xvfb-run -a -s "-screen 0 1920x1200x24" \
    env "${audio_env[@]}" "${out}/run/sierra-hd" "${args[@]}" /game > "${out}/log.txt" 2>&1 || status=$?
wall=$(( $(date +%s) - start ))
rm -f "${out}/audio.raw"

# Log lines that count: every [<category>:error] or [<category>:warning]
# line, an assertion, the interpreter's stop, anything else that names an
# error. Accepted (not counted): SDL's or Xvfb's own lines about the X
# server (xkbcomp, "XKEYBOARD"), which come from the headless display.
errors="$(grep -E ':error\]|assertion failed|The interpreter stopped|Cannot |[Ee]rror' "${out}/log.txt" \
    | grep -vE 'XKEYBOARD|xkbcomp|^> ' || true)"
warnings="$(grep -E ':warning\]' "${out}/log.txt" || true)"
error_count="$(printf '%s' "${errors}" | grep -c . || true)"
warning_count="$(printf '%s' "${warnings}" | grep -c . || true)"

# The end state, after the first field (the tick requested).
end_check=missing
if [[ -f "${out}/end-state.txt" ]]; then
    if diff <(cut -d' ' -f2- "${reference}") <(cut -d' ' -f2- "${out}/end-state.txt") > "${out}/end-state.diff"; then
        end_check=same
        rm -f "${out}/end-state.diff"
    else
        end_check=different
    fi
fi
end_tick="$(head -1 "${out}/end-state.txt" 2>/dev/null | cut -d' ' -f2)"

rm -rf "${out}/run"
result="${name} status=${status} wall=${wall}s errors=${error_count} warnings=${warning_count} end_tick=${end_tick:-none} end_state=${end_check}"
echo "${result}" | tee "${out}/result.txt"
if [[ -n "${errors}" ]]; then printf 'errors:\n%s\n' "${errors}"; fi
if [[ -n "${warnings}" ]]; then printf 'warnings:\n%s\n' "${warnings}"; fi
if [[ "${status}" != 0 || "${error_count}" != 0 || "${warning_count}" != 0 || "${end_check}" != same ]]; then
    exit 1
fi
