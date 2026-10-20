#!/usr/bin/env bash
# Runs clang-tidy on every project source in a configured build tree.
#   scripts/run-clang-tidy.sh build/debug
set -euo pipefail
cd "$(dirname "$0")/.."

build_dir="${1:?usage: run-clang-tidy.sh <build directory with compile_commands.json>}"
# Tracked and new (not ignored) sources that exist in the working tree.
sources=()
while IFS= read -r file; do
    [[ -f "${file}" ]] && sources+=("$PWD/${file}")
done < <(git ls-files --cached --others --exclude-standard -- '*.cpp' ':!:third_party/**')
run-clang-tidy -quiet -p "${build_dir}" -j "$(nproc)" "${sources[@]}"
