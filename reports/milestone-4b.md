# Milestone 4b: kernel coverage

Status: **for review**.

Exit criteria (spec section 12): every kernel function the game's scripts
reference is implemented, and scenario recordings covering the main areas
of the game give matching traces in both engines. Added at the 4a gate:
the game runs in the program, with the analysis cache of spec section
10.2.

## What was implemented

The decisions are in `docs/ASSUMPTIONS.md`, "Milestone 4b" (two sections:
kernel coverage, and the analysis cache); the formats in
`docs/formats/replay-and-trace.md` and `docs/formats/analysis-cache.md`;
the structure in `docs/ARCHITECTURE.md` and `src/sci-vm/README.md`.

1. **The referenced kernel functions.** `sci-vm-trace --scan-kernel`
   lists every `callk` in the scripts' code, after the script patches: the
   tested release references 97 kernel functions, and all 97 are ported.
   What remains unported in the SCI0 table is DoAvoider and ScummVM's own
   debug-only entries; no script calls them. New in 4b:
   - **the text parser** (vocabulary, tokenizing, synonyms, the grammar,
     Said specs) in sci-text, with kParse, kSaid and kSetSynonyms;
   - **the interactive menu and the dialog controls**: kMenuSelect,
     kDrawControl, kHiliteControl, kEditControl;
   - **files**: kFOpen, kFGets, kFPuts, kFClose, kDeviceInfo, through a
     `FileStore` the host supplies (sci-vm does no I/O);
   - **saving**: kGetSaveFiles, kCheckSaveGame, kGetSaveDir work as in
     ScummVM with no saves; kSaveGame and kRestoreGame report failure to
     the game, as ScummVM does when it cannot save, until the save format
     of milestone 8 (decided at the start of 4b).
2. **Fixes to the deterministic mode**, in both engines:
   - the time of day runs with the virtual clock from noon on 1990-01-01
     (a fixed time froze the opening and timed conversations);
   - a script that waits by reading kGetTime over and over sees time pass
     (the copy-protection answer hung both engines);
   - ScummVM's screen-update throttle, where a quit takes effect, and the
     text-edit cursor blink run on the virtual clock (they used the host
     clock, so where a run stopped depended on the machine's speed);
   - the reference ends the process a minute past its stop limits if the
     game never polls events.
3. **Screens are compared as well as kernel calls.** Both engines write the
   screen the player sees once a second of virtual time;
   `compare-screens.py` compares them pixel for pixel. A kernel-call trace
   cannot see drawing: a wrongly signed colour once filled every text box
   white while the traces matched.
4. **The game in the program** (`sierra-hd`, the default; `--viewer` keeps
   the room viewer of v0.1):
   - the interpreter runs on the main thread; wherever virtual time passes,
     the program presents the frame, reads input and waits that long in
     real time;
   - HD mode: the HD background where nothing covers the picture, every
     other pixel (sprites, text, windows, the menu bar) as an N x N block
     of its EGA colour, until the HD sprites of milestone 5; F12 switches
     to original mode at any time; F11 or Alt+Enter toggles fullscreen;
   - the game's cursor as the window's cursor, keys translated as ScummVM
     translates them, the host's local time;
   - the game's own files in `files/` next to the configuration;
   - `--replay <file> [--seed <n>]` plays a scenario into the window.
5. **The analysis cache**: all pictures are analysed on first launch,
   behind a progress bar, and kept in `cache/` next to the configuration,
   keyed by the game data, the analyser version, the hint file and the
   variant. The game and the viewer read their HD scenes from it.
6. **Scenario tools**: `tools/vm-trace/run-scenario.sh` runs one replay in
   both engines and compares traces and screens; `run-all-scenarios.sh`
   runs every entry of `tests/scenarios/scenarios.txt`.

## Results

### Scenarios

19 runs of 17 recordings, in `tests/scenarios/` (each file says what it
does). Every run compares the whole trace, call by call, and every screen
written, from the start to the stop tick:

| Run | Input | Seed | Ticks | Kernel calls | Screens | Result |
|---|---|---|---|---|---|---|
| boot | none | 0 | 3,600 | 12,022 | 61 | identical |
| boot | none | 7 | 3,600 | 12,022 | 61 | identical |
| boot | none | 12345 | 3,600 | 12,022 | 61 | identical |
| boot | none | 0 | 18,000 | 57,622 | 301 | identical |
| mouse | moves and a click | 0 | 3,600 | 13,864 | 31 | identical |
| text | "look" on the title | 0 | 3,600 | 34,624 | 61 | identical |
| copy-protection | Enter, a click | 0 | 6,000 | 57,406 | 101 | identical |
| menu-keys | menu bar by keys | 0 | 3,600 | 29,745 | 61 | identical |
| intro | "No", then the opening | 0 | 30,000 | 227,303 | 501 | identical |
| mansion-walk | arrow keys, 6 rooms | 0 | 11,000 | 81,728 | 184 | identical |
| menus | every harmless menu entry | 0 | 9,030 | 67,952 | 151 | identical |
| bedroom-wait | none (Act I) | 0 | 20,000 | 119,178 | 334 | identical |
| parser-look | typed: look | 0 | 7,700 | 74,717 | 129 | identical |
| parser-verbs | typed: verbs | 0 | 7,700 | 73,547 | 129 | identical |
| parser-errors | typed: unknown words, bad sentences | 0 | 7,700 | 73,875 | 129 | identical |
| mouse-walk | mouse only, 8 rooms | 0 | 12,600 | 109,769 | 211 | identical |
| grounds | outside, 5 areas | 0 | 12,600 | 110,283 | 211 | identical |
| inventory-and-actions | objects, the soup bone | 0 | 24,000 | 226,195 | 401 | identical |
| boot-and-bedroom | copy protection, seed 7 | 7 | 5,000 | 39,019 | 84 | identical |

The scenarios cover the title and copy protection (three seeds, and the
correct answer for seeds 0 and 7), the whole opening into Act I, the
menus and every harmless menu entry, typed commands (looking, verbs,
unknown words, bad sentences), the inventory (the soup bone is taken from
the icebox, looked at and given to the dog), walking by keyboard and by
mouse through 8 rooms of the house and 7 areas of the grounds, and the
first scene of Act I with nobody at the keyboard.

Each run was compared against one reference run in three builds of
sci-vm: debug, ASan with UBSan (no sanitizer reports), and ARM64 under
qemu-user. All 57 comparisons are identical.

### What the comparison found, now fixed

- kDisplay read its background colour 65535 as a colour instead of none,
  so text boxes were filled white; kTextSize read its maximum width
  65535 the same way, so icon messages got a narrow, tall window. Both
  are int16 in ScummVM. An audit of all 97 functions against ScummVM
  found four more such places, which the scenarios do not reach; they are
  fixed too (TextSize's font, DrawPic's palette, the priority byte, ABS on
  int16).
- kFGets returned end of file for an empty line.
- A script shifts a value by 65535, which C++ leaves undefined (so does
  ScummVM's code). sci-vm now does what the x86-64 reference does, on
  every architecture; UBSan found it.

### The program

- The first launch analyses all 176 entries (88 pictures, 2 planes) in
  2.8 s in the release build on 4 cores, against the 15 s budget; a
  scene loads from its file in about 4 ms. The cache takes 83 MB.
- I checked the program under a virtual display (screenshots): the
  progress bar, the copy protection, the bedroom in HD with Laura and
  the menu bar on top, and a replay played into the window. No manual
  play beyond that: please try it.

## Tests and style

- All 22 test suites pass in the debug and ASan builds; the sci-vm and
  parser tests also pass on ARM64. New: files, the path matcher, the
  clock's busy-wait rule, the event queue, the parser (vocabulary,
  grammar, Said), key translation and cursors, the analysis cache.
- All 11 fuzz targets ran for 60 s with no findings, among them the new
  parser and scene targets.
- clang-format, clang-tidy and `scripts/check-repo-policy.py` report no
  findings.
- CI was not run (GitHub CI limits exceeded); all checks above ran
  locally in the toolchain container.

## Deviations from the spec

None from section 2. For your confirmation (`docs/ASSUMPTIONS.md`,
marked **Proposed**):

- **HD mode while the game runs** draws everything but the picture as
  EGA blocks; HD sprites are milestone 5.
- **The game's own files** go in `files/` next to the configuration.
- **What the first launch analyses**: each picture as drawn without
  options; mirrored and EGA-palette variants are analysed when first
  needed, and overlays are not cached.

## Open questions

1. **The three proposals above**: are they acceptable?
2. **The cache's size** (83 MB): acceptable, or should the entries be
   compressed, at the cost of a more complex format?
3. **The deterministic mode's time of day** now starts at noon on
   1990-01-01 and runs. The opening and conversations need it to run;
   do you agree with the start date and time?
