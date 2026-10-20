#!/usr/bin/env bash
# Checks (or with --fix, applies) clang-format on all project C++ sources.
set -euo pipefail
cd "$(dirname "$0")/.."

mode=(--dry-run --Werror)
if [[ "${1:-}" == "--fix" ]]; then mode=(-i); fi

# Tracked and new (not ignored) files that exist in the working tree.
mapfile -d '' files < <(git ls-files -z --cached --others --exclude-standard -- '*.cpp' '*.hpp' ':!:third_party/**')
existing=()
for file in "${files[@]}"; do [[ -f "${file}" ]] && existing+=("${file}"); done
if [[ ${#existing[@]} -gt 0 ]]; then
    clang-format "${mode[@]}" "${existing[@]}"
fi
