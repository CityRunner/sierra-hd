#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The local checks that must pass before a push (docs/ASSUMPTIONS.md, "CI
# cost policy"): CI confirms them, it does not discover failures. Runs in
# the toolchain image, in this order, stopping at the first failure:
#
#   1. clang-format, the repository policy, the hint files, clang-tidy;
#   2. build and unit tests: release, ASan+UBSan, TSan (all tests);
#   3. short fuzzing (SCI_FUZZ_SECONDS per target, default 30);
#   4. ARM64 under qemu-user: build and every test, among them the golden
#      hashes of synthetic scenes, which must equal the x86-64 ones.
#
#   scripts/pre-push.sh            # everything
#   scripts/pre-push.sh --quick    # 1 and the release build and tests only
#
# The long checks with game files (golden hashes of all pictures, scenario
# recordings against the reference) are separate: tools/golden-hashes and
# tools/vm-trace/run-all-scenarios.sh.
set -euo pipefail
cd "$(dirname "$0")/.."

quick=false
if [[ "${1:-}" == "--quick" ]]; then quick=true; fi
seconds="${SCI_FUZZ_SECONDS:-30}"

step() { printf '\n== %s\n' "$1"; }

run() {
    docker/run.sh bash -c "$1"
}

step "style and static analysis"
run "scripts/check-format.sh && python3 scripts/check-repo-policy.py && python3 scripts/check-hints.py \
     && cmake --preset debug > /dev/null && scripts/run-clang-tidy.sh build/debug"

step "release: build and tests"
run "cmake --workflow --preset release"
if [[ "${quick}" == true ]]; then
    printf '\nQuick checks passed (not enough for a push: run without --quick).\n'
    exit 0
fi

step "ASan+UBSan: build and tests"
run "cmake --workflow --preset asan"

step "TSan: build and tests"
run "cmake --workflow --preset tsan"

step "fuzzing (${seconds} s per target)"
run "cmake --preset fuzz > /dev/null && cmake --build --preset fuzz && scripts/run-fuzzers.sh build/fuzz ${seconds}"

step "ARM64 under qemu-user: build and tests"
run "cmake --workflow --preset cross-aarch64"

printf '\nAll pre-push checks passed.\n'
