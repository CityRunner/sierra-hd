# Contributing

Issues are welcome: bug reports (with the release you use, your operating
system and the steps that lead to the problem), differences from the
original game or from ScummVM, and ideas. The project follows a written
specification milestone by milestone, so larger changes are discussed in an
issue first.

Pull requests:

- Never add game data: resources, ROMs, screenshots or dumps of the game,
  or anything derived from them that could recreate them.
  `scripts/check-repo-policy.py` and `.gitignore` guard against the common
  cases.
- Follow the style of the surrounding code (`.clang-format`,
  `.clang-tidy`, C++23, no `float`; see `docs/ARCHITECTURE.md`).
- Run `scripts/pre-push.sh` before pushing. It builds and tests in the
  pinned toolchain container (`docker/`): formatting, clang-tidy, the unit
  tests in release, ASan+UBSan and TSan, short fuzzing, and ARM64 under
  qemu-user.
- Code ported from ScummVM keeps ScummVM's licence notice and names the
  functions and files it comes from (`THIRD_PARTY.md`).
- CI runs on pull requests (`docs/ASSUMPTIONS.md`, "CI cost policy"): a
  short Linux run for drafts, the full matrix when the pull request is
  ready for review.

By contributing you agree that your contribution is licensed under the GPL,
version 3 or later, like the rest of the project.
