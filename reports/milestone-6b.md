# Milestone 6b: text windows, menus and the mouse cursor

Status: **awaiting approval**.

Exit criterion (spec section 12): window layout matches ScummVM in 1x
coordinates, and windows render correctly at every N.

## What was implemented

The decisions are in `docs/ASSUMPTIONS.md`, "Milestone 6b: text windows,
menus and the mouse cursor". The file format is in
`docs/formats/replay-and-trace.md`, "Layout files", and the HD side is in
`docs/hd-composition.md`.

1. **The window layout is compared directly, in 1x.** Before, a layout
   difference showed only through the screens. Now both engines write a
   layout file in screen coordinates, keyed by tick and kernel call:
   - **windows:** port, frame, saved area, style, priority, colours and
     title, and when each is disposed of;
   - **text:** measured sizes (kTextSize and every other size), text boxes
     (rectangle, alignment, font) and each drawn line (position, width,
     font, colour, greyed);
   - **kDisplay** rectangles and the areas they save;
   - **controls:** type, object, rectangle, state, font and highlight;
   - **inverted rectangles:** selections and the edit cursor;
   - **the menu bar and open menus;**
   - **kGraph boxes,** saved and restored;
   - **the cursor:** each change of shape (hotspot and all 256 pixels), show
     and hide, and the positions the game sets.

   The reference writes the file from hooks in patch 0003, and sci-vm from
   the same places in its port. `tools/vm-trace/run-scenario.sh` compares
   it as a sixth item, after the traces, screens, maps, actor positions and
   parser results (`tools/vm-trace/compare-layout.py`).
2. **Two new scenarios.**
   - `menus-mouse` chooses every item of the menu bar with the mouse.
   - `menus-keys-2` does three things:
     - chooses the items the keyboard scenarios left out;
     - clicks in a menu opened with the keyboard;
     - presses every shortcut key.
   - The save, restore, restart and quit dialogs are opened and cancelled;
     nothing is saved (saves are milestone 8).

   With the earlier scenarios, the corpus covers:
   - 3,169 windows in 123 sizes, with 1 to 18 rows of text;
   - titled windows, windows at the screen's edges and over sprites;
   - every control type;
   - the game's three cursors, hidden and shown, and 15 cursor positions
     set by the game.
3. **Windows at every N.** `sci-hd-frames --check-interface 1-16` runs a
   replay and checks each frame where the interface or the picture
   changed. It checks at every N from 1 to 16, the largest N the program
   supports, in original and HD mode. The frames are the program's own
   (`GameFrame`), updated tile by tile as in the program. At each frame and
   each N:
   - in original mode, every pixel is its 1x colour as an N × N block;
   - in HD mode, every interface pixel is its 1x colour as an N × N block;
   - with the interface recoloured, the frame changes in those blocks and
     nowhere else: no HD bleeds into a window, and no window into HD;
   - the menu bar's rows (0–9) are interface.
4. **The cursor and the mouse.**
   - **Cursor image.** The cursor decoder moved into sci-view, where the
     interpreter's layout file uses it too. The program scales the cursor
     N × N, with its hotspot scaled N times.
   - **System cursor.** The program uses it only where the system shows it
     pixel for pixel: a window pixel density of 1, and at most 256 pixels a
     side.
   - **Drawn cursor.** Elsewhere the program draws the scaled cursor over
     the frame, 1:1, with its hotspot on the mouse's physical pixel. This
     covers macOS Retina and scaled Wayland, and a failed system cursor.
   - **Mouse mapping** is now one tested function: the screen position, to
     the physical pixel, minus the letterbox, divided by N, clamped to
     320 × 200. The old code rounded towards zero; it gave the same results
     only because of the clamping.
5. **Tools.** `tools/vm-trace/run-all-scenarios.sh --jobs <n>` runs
   scenarios in parallel. The interface check can resume where it stopped.

## Results

### Scenarios: six comparisons

All 33 runs of `tests/scenarios/scenarios.txt` were run in the reference
and in sci-vm, in the main checkout after the merge, with the reference
rebuilt from the updated patch. Everything matched on all six items:

| Scenario | Stop tick | Seed | Kernel calls | Screens and maps | Actor frames | Parser lines | Layout lines |
|---|---|---|---|---|---|---|---|
| boot | 3,600 | 0 | 12,022 | 61 | 606 | 0 | 101 |
| boot | 3,600 | 7 | 12,022 | 61 | 606 | 0 | 101 |
| boot | 3,600 | 12345 | 12,022 | 61 | 606 | 0 | 101 |
| boot | 18,000 | 0 | 57,622 | 301 | 3,006 | 0 | 101 |
| mouse | 3,600 | 0 | 13,864 | 31 | 157 | 0 | 114 |
| text | 3,600 | 0 | 34,624 | 61 | 111 | 0 | 123 |
| copy-protection | 6,000 | 0 | 57,406 | 101 | 161 | 0 | 114 |
| menu-keys | 3,600 | 0 | 29,745 | 61 | 224 | 0 | 149 |
| intro | 30,000 | 0 | 227,303 | 501 | 4,759 | 0 | 908 |
| mansion-walk | 11,000 | 0 | 81,728 | 184 | 1,723 | 0 | 394 |
| menus | 9,030 | 0 | 67,952 | 151 | 1,107 | 0 | 800 |
| bedroom-wait | 20,000 | 0 | 119,178 | 334 | 3,318 | 0 | 279 |
| parser-look | 7,700 | 0 | 74,717 | 129 | 920 | 1,136 | 909 |
| parser-verbs | 7,700 | 0 | 73,547 | 129 | 930 | 907 | 697 |
| parser-errors | 7,700 | 0 | 73,875 | 129 | 920 | 949 | 748 |
| mouse-walk | 12,600 | 0 | 109,769 | 211 | 1,400 | 234 | 466 |
| grounds | 12,600 | 0 | 110,283 | 211 | 1,532 | 285 | 617 |
| inventory-and-actions | 24,000 | 0 | 226,195 | 401 | 2,394 | 1,297 | 1,509 |
| boot-and-bedroom | 5,000 | 7 | 39,019 | 84 | 730 | 285 | 305 |
| walls-keyboard | 9,300 | 0 | 61,779 | 156 | 1,522 | 0 | 356 |
| mouse-obstacles | 8,500 | 0 | 56,963 | 142 | 1,383 | 111 | 354 |
| stairs | 8,200 | 0 | 59,428 | 137 | 1,326 | 60 | 363 |
| lillian-walk | 17,400 | 0 | 107,135 | 291 | 2,865 | 31 | 321 |
| attic | 9,400 | 0 | 76,231 | 157 | 1,119 | 80 | 325 |
| parser-forms | 15,122 | 0 | 249,195 | 253 | 1,394 | 10,104 | 9,052 |
| parser-forms-2 | 19,555 | 0 | 305,534 | 326 | 1,913 | 15,879 | 9,173 |
| parser-words-hallway | 39,821 | 0 | 785,671 | 664 | 3,514 | 55,148 | 24,162 |
| parser-words-garden | 39,854 | 0 | 715,093 | 665 | 3,329 | 42,138 | 28,549 |
| parser-words-kitchen | 39,803 | 0 | 647,968 | 664 | 3,566 | 41,770 | 17,796 |
| parser-words-bedroom | 24,024 | 0 | 495,499 | 401 | 2,058 | 39,137 | 19,457 |
| parser-words-downstairs | 22,057 | 0 | 378,735 | 368 | 2,046 | 25,189 | 13,639 |
| **menus-mouse** (new) | 8,700 | 0 | 78,564 | 146 | 954 | 5 | 1,156 |
| **menus-keys-2** (new) | 9,500 | 0 | 81,649 | 159 | 927 | 0 | 1,135 |

- **33 runs, 134,374 layout lines:** every trace, screen, map, actor frame,
  parser line and layout line identical.
- **No port bug turned up.** sci-vm already laid out every window, text
  line, control and menu as ScummVM does.
- **The comparison catches differences.**
  - Moving one window's left edge by one pixel in a kept `menus-mouse` run
    makes `compare-layout.py` report that window and exit with 1.
  - Widening sci-vm's menus by one pixel fails both the screens and the
    layout comparison.

### Windows at every N

The every-N check ran on all 30 replays of `scenarios.txt`, each once, at
its longest stop tick:

- **Frames:** 23,630 frames where the interface or the picture changed.
  - 21,782 of them show interface below the menu bar.
  - 17,802 show a window over a sprite.
- **Checks:** 378,080 checks of a frame at one N, each composed in original
  mode, in HD mode, and in HD with the interface recoloured: about 1.13
  million compositions.
- **Failures:** none.
- **The check catches bleed.** Making the composition draw some interface
  pixels at N = 7 through the HD path gave 107 failures, caught by the
  recoloured-interface test.
- **Coverage by scenario.** The five parser-words scenarios give 18,875 of
  the frames; `menus-mouse` 156, `menus-keys-2` 136,
  `inventory-and-actions` 227 and `intro` 242.

### Images

The images are in `reports/images/milestone-6b/` (git-ignored, game art),
listed in its `README.txt`. Each shows original and HD side by side:

| Images | What they show |
|---|---|
| `menus-mouse-n5/*` | the About window near both side edges; the Action menu open; the save and restore dialogs with title, edit field, list and buttons; the quit question; the inventory window over Laura; the input line; the volume window with its edit field |
| `menus-mouse-n2/*`, `menus-mouse-n10/*` | the open menu and the save dialog at N = 2 and N = 10 |
| `inventory-n5/*` | the input line while typing; the inventory window indoors and outdoors; a message window at the bottom left |
| `cursors.png` | the game's three cursors (1, copy protection; 997, wait; 999, arrow) at N = 1, 2, 5 and 10, with their hotspots |

## Tests and style

- **Local checks.** `scripts/pre-push.sh` passes on the merged branch, run
  step by step (the rule of small tasks):
  - clang-format, clang-tidy, the repository policy and the hint check;
  - all test suites in the release, ASan+UBSan and TSan builds;
  - fuzzing;
  - all tests on ARM64 under qemu-user.
- **New unit tests:**
  - **The every-N checks** on a synthetic screen at N = 1–16. Its HD
    background differs in every sub-pixel, and a window covers a sprite.
    Further tests show the checks catch a wrong sub-pixel, a block next to
    a window that changes with it, and a menu bar pixel that is not
    interface. One test runs through `GameFrame`'s tile updates.
  - **The cursor:** the scaled cursor equals the resource at N × N, for
    N = 1–16, with the hotspot scaled. The cursor decoder's tests moved to
    sci-view, and the decoder is fuzzed.
  - **The mouse mapping:**
    - letterbox offsets;
    - pixel densities 1, 1.25, 1.5 and 2;
    - clamping;
    - a round trip from a 1x pixel to the centre of its block and back.

## CI minutes

**The gate is green** (run 39932177249, commit `19b676c`). It ran style
and static analysis; Linux x86-64 and ARM64 in release, ASan+UBSan and
TSan; the fuzzers; and Windows x64 and ARM64. macOS is optional and was not
run.

Measured with `scripts/ci-minutes.py --branch milestone-6b`:

| Platform | Jobs | Minutes | Included minutes | List price |
|---|---|---|---|---|
| Linux x86-64 | 6 | 51 | 51 | $0.31 |
| Linux ARM64 | 3 | 20 | 20 | $0.10 |
| Windows x64 | 1 | 6 | 12 | $0.06 |
| Windows ARM64 | 1 | 4 | 8 | $0.04 |
| **Total** (1 run) | | **81** | **91** | **$0.51** |

- **Cost:** none. The repository is public, so standard runners are free.
- **Runs:** one, the gate run of this pull request. Every check passed
  locally first, so nothing had to be rerun. The commit that records the
  rule of small tasks, and this report's own commit, change only
  documentation and start no run.

## Deviations from the spec

None.

## Open questions

None. Some things could not be tested here:
- **The drawn cursor** (high-density displays) was tested under Xvfb only,
  by forcing it on for a run. The frame showed the 3× arrow with its
  hotspot on the expected pixel. Xvfb has no high-density display, so the
  real case needs a Retina or scaled Wayland screen: the playthrough of
  milestone 9 or a manual check.
- **A 256-pixel system cursor** (N = 16) was not tried on a real X11 or
  Windows desktop.

## Working rule added during this milestone

Two runs of this milestone stopped at a usage limit for about three hours
each. At the customer's request, work is now split into small tasks
(`docs/ASSUMPTIONS.md`, "Small agent tasks"):
- each task finishes well within an hour;
- long checks run in resumable pieces;
- work is committed before a long check starts.

The scenario runs and the local checks for this report already ran that way.
