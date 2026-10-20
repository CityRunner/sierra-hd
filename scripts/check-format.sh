#!/usr/bin/env bash
# Checks (or with --fix, applies) clang-format on all project C++ sources.
set -euo pipefail
cd "$(dirname "$0")/.."

mode=(--dry-run --Werror)
if [[ "${1:-}" == "--fix" ]]; then mode=(-i); fi

git ls-files -z -- '*.cpp' '*.hpp' ':!:third_party/**' | xargs -0 --no-run-if-empty clang-format "${mode[@]}"
