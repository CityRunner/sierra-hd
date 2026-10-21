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
kernel-call traces and the screens they show must match.
`scenarios/scenarios.txt` lists the runs (file, stop tick, seed), and

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh tools/vm-trace/run-all-scenarios.sh

runs them all (`tools/vm-trace/run-scenario.sh` runs one).

- `boot.txt` (seeds 0, 7 and 12345 to tick 3600; seed 0 to tick 18000): no
  input; the title and the copy-protection screen.
- `mouse.txt` (seed 0, stop tick 3600): mouse moves and a click on the
  copy-protection screen.
- `text.txt` (seed 0, stop tick 3600): types "look" and Enter on the title.

- `menu-keys.txt` (seed 0, stop tick 3600): opens the menu bar with
  Escape, walks it with the arrow keys, closes it, then opens it again and
  chooses the first item of the first menu.
- `copy-protection.txt` (seed 0, stop tick 6000): leaves the title with
  Enter and clicks the first name of the copy-protection screen (the
  dialog controls).
- `intro.txt` (seed 0, stop tick 30000): copy protection, then "No" to
  "Have you previously attended?" (a click on the button), which plays the
  whole opening without further input (the will, the theatre, the
  invitation, the credits, the arrival, the dinner) into Act I.
- `mansion-walk.txt` (seed 0, stop tick 11000): from Laura's bedroom
  (room 44), arrow keys only, through Ethel's room (45), the back and front
  upstairs hallways (43, 47), down the grand staircase into the front
  downstairs hallway (37) and back, into the Colonel's room (42), where the
  clock strikes and a conversation starts.
- `menus.txt` (seed 0, stop tick 9030): in the bedroom, every harmless
  menu entry (About, Help, Inventory, Pause, the three speeds, Volume) and
  the keys F1, F2, Tab, Ctrl+P, Ctrl+V and Ctrl+I, each dialog closed with
  Enter or Escape.
- `bedroom-wait.txt` (seed 0, stop tick 20000): Act I starts in the
  bedroom and runs without input; Lillian comes in and sits down.

Every run, of the reference and of `sci-vm-trace`, gives a stop tick:

    sci-vm-trace <game> <trace> --seed 0 --stop-tick 3600 \
        --replay tests/scenarios/menu-keys.txt
