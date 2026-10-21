# tests

- `support/`: the minimal test harness (`SCI_TEST`, `SCI_CHECK`).
- `unit/`: unit tests, one executable per library (`sci-platform` included:
  configuration and release identification).
- `fuzz/`: libFuzzer targets with their seed corpora. Without `SCI_FUZZ`,
  each target replays its corpus as a CTest test.
- `golden/`: golden hashes. `synthetic_test.cpp` renders synthetic pictures
  and runs in CI on every platform; `pictures.txt` holds the hashes of every
  picture of the tested release, checked locally.
- `scenarios/`: scenario recordings, replays in the format of
  `docs/formats/replay-and-trace.md`, run against the game locally.

Seed corpora are hand-made; `fuzz/make_seeds.py` regenerates the
sci-resource ones. libFuzzer runs write new inputs to a scratch directory
(`scripts/run-fuzzers.sh`), never into the corpora.

Tests that need the game files run locally, never in CI:

- Resources (milestone 1): `tools/resource-compare/README.md`.
- Pictures, views and fonts (milestone 2): `tools/graphics-compare/README.md`.
- The HD pipeline (milestone 3): `tools/hd-report/README.md` (acceptance
  check) and `tools/golden-hashes/README.md` (golden hashes, also on ARM64
  under qemu-user).
- Hints (milestone 3a): the hint-anchor check, `sci-hint-tool <game> check
  hints` (`tools/hint-tool/README.md`). CI checks the hint files against
  their schema without the game (`scripts/check-hints.py`).

## Scenario recordings

Each file in `scenarios/` is a hand-written replay, timed in game ticks.
Both interpreters run it with the same seed and a stop tick, and their
kernel-call traces must match (`tools/vm-trace/compare.py`):

- `menu-keys.txt` (seed 0, stop tick 3600): opens the menu bar with
  Escape, walks it with the arrow keys, closes it, then opens it again and
  chooses the first item of the first menu.
- `copy-protection.txt` (seed 0, stop tick 3000): leaves the title with
  Enter and clicks the first name of the copy-protection screen (the
  dialog controls).

Every run, of the reference and of `sci-vm-trace`, gives a stop tick:

    sci-vm-trace <game> <trace> --seed 0 --stop-tick 3600 \
        --replay tests/scenarios/menu-keys.txt
