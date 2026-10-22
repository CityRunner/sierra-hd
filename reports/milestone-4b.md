# Milestone 4b: kernel coverage

Status: **approved** (2026-10-22). The gate matrix is green; the customer's
"go ahead for next step" after it is taken as the approval.

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

- All 23 test suites pass in the release, ASan+UBSan and TSan builds and
  on ARM64 under qemu-user (`scripts/pre-push.sh`). New tests cover:
  - files and the path matcher;
  - the clock's busy-wait rule and the event queue;
  - the parser (vocabulary, grammar, Said);
  - key translation and cursors;
  - the analysis cache;
  - the code on worker threads (`tests/threads`, label `threads`). Until
    now no test ran on more than one thread, so TSan had nothing threaded
    to check.
- All 11 fuzz targets ran for 60 s with no findings, among them the new
  parser and scene targets.
- clang-format, clang-tidy and `scripts/check-repo-policy.py` report no
  findings over the whole tree. An earlier version of this report said so
  after checking only the changed files: the first full run found the
  script scanner (`scan.cpp`) over the complexity limit, now split.
- **A CI defect, fixed:** the Linux jobs never failed on a build, lint or
  test error from milestone 0 on. Their `... | tee build.log` steps ran
  without `pipefail`, so they took `tee`'s status. The CI lint job had
  passed on `scan.cpp`. Windows and macOS were not affected. Details are in
  `docs/ASSUMPTIONS.md`, "CI cost policy".
- All checks above ran locally in the toolchain container first
  (`scripts/pre-push.sh`); CI confirms them at the gate (below).

## CI minutes

**The gate is green** (run 39733318155, commit `e0983a4`): style and
static analysis; Linux x86-64 and ARM64 in release, ASan+UBSan and TSan;
the fuzzers; Windows x64 (release and ASan) and ARM64 (release). macOS is
optional and was not run.

- **The two gate runs before it** found a macOS-only build problem:
  `std::ranges::iota` is missing from Apple's libc++. It is fixed. Two
  further macOS gaps are recorded for when macOS is wanted.
- **The first gate under the policy** could not start, because the
  account's minutes were used up. It ran once the repository was public.

Under the CI cost policy (`docs/ASSUMPTIONS.md`, "CI cost policy"),
introduced during this gate, measured with
`scripts/ci-minutes.py --branch milestone-4b`:

| Platform | Jobs | Minutes | Included minutes | List price |
|---|---|---|---|---|
| Linux x86-64 | 118 | 630 | 630 | $3.78 |
| Linux ARM64 | 67 | 239 | 239 | $1.20 |
| Windows x64 | 6 | 36 | 72 | $0.36 |
| Windows ARM64 | 5 | 19 | 38 | $0.19 |
| macOS | 2 | 17 | 170 | $1.05 |
| **Total** (29 runs) | | **941** | **1,149** | **$6.58** |

- **The runs of 2026-10-22** (188 minutes) ran after the repository was
  made public. Standard runners cost nothing for public repositories, so
  these minutes were free.
- **The two macOS jobs** (17 minutes) ran in the gate runs before macOS
  became optional again.

- Most of these minutes came before the policy: every push ran 8 Linux
  jobs, and 20 of the 4b push runs were cancelled part-way by the next
  push. Under the policy a push runs nothing.
- Two runs were cancelled by hand when the policy arrived: the push run
  that duplicated the pull request run of the same commit, and the
  release build on the pull request (release binaries now build for tags
  only).
- No macOS, Windows or native ARM64 run happened between gates.
- What could reduce the minutes further:
  - a self-hosted Linux runner on a machine of yours, which would make the
    Linux jobs free;
  - ccache, whose savings show from the second gate run on.

## Deviations from the spec

None from section 2 (addendum A2 changes the output decision of section 2
with your approval, from milestone SU on).

## Decisions at the gate (2026-10-21 and 22)

All points of this report were accepted:
- **HD mode in the game:** HD drawing with EGA blocks for everything but
  the picture is accepted until milestone 5. The same goes for `files/`
  next to the configuration, what the first launch analyses, the cache's
  83 MB, and the test mode's time of day from noon on 1990-01-01.
- **Spending limit:** no amount was needed in the end. The repository was
  made public on 2026-10-22, and its standard GitHub-hosted runners cost
  nothing.
- **Self-hosted runners:** accepted, but not set up. They need a machine of
  yours, and they are not recommended for a public repository.
- **Platforms:** macOS is optional again; Linux is the first priority and
  Windows the second. The gate is the Linux and Windows matrix. macOS runs
  only when asked for, and the fix it prompted (`std::ranges::iota`) is
  kept. The optional macOS build still fails, because the libc++ of Apple's
  SDK lacks `std::jthread` and `std::chrono::zoned_time`. Both are recorded
  in `docs/ASSUMPTIONS.md` for when macOS is wanted.
- **Addendum A2** (sprite undithering) is recorded and scheduled as
  milestone SU, after milestone 6.
