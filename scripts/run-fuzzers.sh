#!/usr/bin/env bash
# Runs every libFuzzer target of a fuzz build (preset "fuzz") for a fixed time
# each, starting from its seed corpus. New inputs go to a scratch corpus
# directory, never into the repository; with SCI_FUZZ_CORPUS=<dir> they are
# kept there (one folder per target), so later runs start from them (CI
# caches it).
#   scripts/run-fuzzers.sh build/fuzz 60
set -euo pipefail
cd "$(dirname "$0")/.."

build_dir="${1:?usage: run-fuzzers.sh <fuzz build directory> [seconds per target]}"
seconds="${2:-60}"
if [[ -n "${SCI_FUZZ_CORPUS:-}" ]]; then
    scratch="${SCI_FUZZ_CORPUS}"
    mkdir -p "${scratch}"
else
    scratch="$(mktemp -d)"
    trap 'rm -rf "${scratch}"' EXIT
fi
artifacts="${PWD}/build/fuzz-artifacts"

while IFS='=' read -r target corpus; do
    [[ -z "${target}" ]] && continue
    binary="$(find "${build_dir}" -type f -name "${target}" -perm -u+x | head -n1)"
    echo "== ${target} (${seconds}s)"
    mkdir -p "${scratch}/${target}"
    # A single input taking more than 10 s is reported as a hang. Crash,
    # leak and out-of-memory inputs are kept in build/fuzz-artifacts.
    mkdir -p "${artifacts}"
    "${binary}" -max_total_time="${seconds}" -timeout=10 -print_final_stats=1 \
        -artifact_prefix="${artifacts}/${target}-" "${scratch}/${target}" "${corpus}"
done < "${build_dir}/fuzz-targets.txt"
