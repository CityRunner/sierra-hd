# Milestone 5: animation, hero movement, priorities, collisions

Status: **for review**.

Exit criteria (spec section 12): scripted walks give the same hero
positions as ScummVM, frame by frame. The HD handling of section 6.8 is
completed: the 1x maps match ScummVM, and each case is shown side by side
(original and HD) in the report.

## What was implemented

The decisions are in `docs/ASSUMPTIONS.md`, "Milestone 5: frame-by-frame
comparison" and "Milestone 5: the HD composition". The HD design is in
`docs/hd-composition.md`, and the formats in
`docs/formats/replay-and-trace.md` and `docs/formats/analysis-cache.md`.

1. **Frame-by-frame comparison with ScummVM.**
   - After every `kAnimate` call, both engines write every cast member, in
     drawing order: its position, z, priority, view, loop, cel and signal.
     Every actor on screen is compared, not only the hero.
   - Both engines also write the 1x priority and control maps the game
     logic reads, at the ticks of the screen dumps.
   - `tools/vm-trace/run-scenario.sh` compares four things for each
     scenario: the kernel-call traces, the screens, the maps and the actor
     positions.
2. **Scripted walks** for movement, priorities and collisions, besides the
   earlier scenarios, which are now compared frame by frame too:
   - `walls-keyboard`: walls, slanted walls, furniture, the four diagonals,
     quick stop and go, and priority bands.
   - `mouse-obstacles`: clicks round furniture (the game has no path
     finding) and turns in the middle of a walk.
   - `stairs`: both staircases and the landing railing.
   - `lillian-walk`: Lillian walks into Laura and round her, and Laura
     walks into Lillian.
   - `attic`: up into the attic stairway (room 74), which draws a picture
     overlay, with the two palettes the room uses.
3. **The HD composition** (spec sections 6.6, 6.8 and 6.9).
   - **Owner map.** The interpreter records what drew each 1x pixel:
     picture, addToPic, sprite, kernel line or box, or interface. It also
     records where each priority value came from, keeps the picture stack
     with its overlays, and keeps the layer under the sprites and the
     sprite lists, per shown pixel.
   - **Drawing a frame.** The program draws the HD background, rendered
     from the vector scene of the picture stack. Sprites go over it, drawn
     as original cels scaled N x N. The HD priority map, built from the
     vector priority scene, hides their HD sub-pixels with smooth edges.
     addToPic cels and their priority are N x N blocks. Interface (text,
     windows, menus) is drawn on top.
   - **Snap switch.** `priority_snap = on` snaps occlusion to the sprite
     grid, as in the original.
   - **Original mode** is unchanged: N x N blocks of the shown 1x screen.
   - **Overlays.** A picture stack with overlays is its own entry in the
     analysis cache. The entries of single pictures are unchanged, so
     nothing is re-analysed.
   - **Frame cost.** Only changed 16 x 16 tiles are recomposed each frame.
4. **Tools.**
   - `tools/hd-frames` renders deterministic offscreen frames (original,
     HD and snapped) at chosen ticks of a replay, with timing.
   - `--stack` renders a picture stack without a run.
5. **Golden hashes of the picture stacks the scripts draw** (spec section
   5.3: "with overlays where a room uses them"). A disassembly of all 52
   kDrawPic calls finds five that add to the picture. Their argument is
   always a constant, so the full list of stacks is known. The evidence is
   in `docs/ASSUMPTIONS.md`, "Milestone 5: the picture stacks":
   - room 74 (the attic stairway): `174+74` and `174.palette1+74`;
   - room 290: `282+182` and `282+382`, picked by the fight script's state;
   - room 353: `42+162`, a stack the earlier list missed;
   - restoring a game saved in room 74: `74+174` and
     `74.palette1+174.palette1`. This needs saves (milestone 8); the hashes
     are recorded now.

   `tools/golden-hashes` hashes each stack in the same five kinds as the
   single pictures: HD at N = 1, 2, 5 and 10, and original at N = 1. It
   draws them on the same path as the analysis cache. The 35 new lines of
   `tests/golden/pictures.txt` match on x86-64 and on ARM64 under qemu, and
   the 440 earlier lines are unchanged.

## Results

### Scenarios: traces, screens, 1x maps and actor positions

Every scenario of `tests/scenarios/scenarios.txt` was run in the reference
and in sci-vm and compared on all four counts. Everything matched:

| Scenario | Stop tick | Seed | Kernel calls | Screens | Map frames | Actor frames |
|---|---|---|---|---|---|---|
| boot | 3,600 | 0 | 12,022 | 61 | 61 | 606 |
| boot | 3,600 | 7 | 12,022 | 61 | 61 | 606 |
| boot | 3,600 | 12345 | 12,022 | 61 | 61 | 606 |
| boot | 18,000 | 0 | 57,622 | 301 | 301 | 3,006 |
| mouse | 3,600 | 0 | 13,864 | 31 | 31 | 157 |
| text | 3,600 | 0 | 34,624 | 61 | 61 | 111 |
| copy-protection | 6,000 | 0 | 57,406 | 101 | 101 | 161 |
| menu-keys | 3,600 | 0 | 29,745 | 61 | 61 | 224 |
| intro | 30,000 | 0 | 227,303 | 501 | 501 | 4,759 |
| mansion-walk | 11,000 | 0 | 81,728 | 184 | 184 | 1,723 |
| menus | 9,030 | 0 | 67,952 | 151 | 151 | 1,107 |
| bedroom-wait | 20,000 | 0 | 119,178 | 334 | 334 | 3,318 |
| parser-look | 7,700 | 0 | 74,717 | 129 | 129 | 920 |
| parser-verbs | 7,700 | 0 | 73,547 | 129 | 129 | 930 |
| parser-errors | 7,700 | 0 | 73,875 | 129 | 129 | 920 |
| mouse-walk | 12,600 | 0 | 109,769 | 211 | 211 | 1,400 |
| grounds | 12,600 | 0 | 110,283 | 211 | 211 | 1,532 |
| inventory-and-actions | 24,000 | 0 | 226,195 | 401 | 401 | 2,394 |
| boot-and-bedroom | 5,000 | 7 | 39,019 | 84 | 84 | 730 |
| **walls-keyboard** (new) | 9,300 | 0 | 61,779 | 156 | 156 | 1,522 |
| **mouse-obstacles** (new) | 8,500 | 0 | 56,963 | 142 | 142 | 1,383 |
| **stairs** (new) | 8,200 | 0 | 59,428 | 137 | 137 | 1,326 |
| **lillian-walk** (new) | 17,400 | 0 | 107,135 | 291 | 291 | 2,865 |
| **attic** (new) | 9,400 | 0 | 76,231 | 157 | 157 | 1,119 |

24 runs: every trace, screen, map and actor frame identical.

- **No port bug turned up.** The interpreter's behaviour is unchanged, and
  the HD composition changes no kernel call: traces and screens are the
  same as at 4b.
- **Moving actors never show in the dumped maps.** kAnimate restores what
  was under them before it returns. Their positions, priorities and
  signals are covered by the actor comparison.

### The HD cases of section 6.8, side by side

The images are in `reports/images/milestone-5/` (git-ignored, game art).
Each shows original, HD and snapped at N = 5:

| Image | Case |
|---|---|
| `01-hallway-37-laura-behind-banister-*` | Laura walking down behind the banister, which is drawn by the picture. HD hides her with a smooth edge; snapped and original hide her in whole 1x pixels. |
| `02-bedroom-44-laura-behind-bed-*` | Laura behind bedroom furniture. The furniture is addToPic, so occlusion is in 1x blocks, as section 6.8 says. |
| `03-bedroom-44-addtopic-furniture-*` | addToPic cels as N x N blocks over the vector background. |
| `04-overlay-room-74-*` | Picture 174 alone, and with picture 74 added as an overlay layer of the vector scene. |
| `05-bedroom-44-window-over-laura-*` | A message window over Laura: the interface stays on top. |
| `06-kernel-lines-window-frame-*` | The double frame of a message window, drawn with kGraph lines in the window manager's port, shown as interface. |

**Kernel primitives drawn into the picture:** no room of this game draws
one. Every kGraph line and box is drawn inside windows. That path is
covered by unit tests only.

### Performance

Release build, 4 cores, `sci-hd-frames --timing`:

| | N = 5 | N = 10 |
|---|---|---|
| Frame with changes, room 37 (Laura on the stairs): mean / max | 0.49 / 1.4 ms | 0.73 / 1.0 ms |
| Frame with changes, room 44: mean / max | 0.59 / 1.1 ms | 0.76 / 1.2 ms |
| Whole frame again (mode or snap switched) | 6.6 ms | 20.7 ms |
| First frame of a new picture, rendering included | 156 ms | 515 ms |

- **Normal frames** are well inside the 16.7 ms of 60 FPS.
- **A whole frame at N = 10** takes 20.7 ms, just over one frame. It
  happens only when entering a room or switching mode or snap.

## Tests and style

- **Local checks.** `scripts/pre-push.sh` passes on the merged branch:
  - clang-format, clang-tidy, the repository policy and the hint check;
  - all 23 test suites in the release, ASan+UBSan and TSan builds;
  - fuzzing;
  - all tests on ARM64 under qemu-user.
- **New unit tests:**
  - the screen bookkeeping in sci-vm: owners, priority source, saved
    bits, background layer and sprite lists;
  - the composition, on a synthetic sprite partly behind a diagonal
    foreground face. It covers HD sub-pixel occlusion against snap mode,
    which equals the 1x blocks, and the interface on top. It also covers
    addToPic priority blocks, original mode and partial recomposition;
  - change detection in the frame, overlay variants of the analysis cache,
    and the `priority_snap` key;
  - a golden hash of a synthetic composed frame, the same on x86-64 and
    ARM64.

## CI minutes

**The gate is green** (run 39811487129, commit `447279f`). It ran style
and static analysis; Linux x86-64 and ARM64 in release, ASan+UBSan and
TSan; the fuzzers; and Windows x64 (release and ASan) and ARM64 (release).
macOS is optional and was not run.

Measured with `scripts/ci-minutes.py --branch milestone-5`:

| Platform | Jobs | Minutes | Included minutes | List price |
|---|---|---|---|---|
| Linux x86-64 | 12 | 111 | 111 | $0.67 |
| Linux ARM64 | 6 | 27 | 27 | $0.14 |
| Windows x64 | 2 | 17 | 34 | $0.17 |
| Windows ARM64 | 2 | 7 | 14 | $0.07 |
| **Total** (2 runs) | | **162** | **186** | **$1.04** |

- **Cost:** none. The repository is public, so standard runners are free.
- **Runs:** only two ran, both gate runs on this pull request. Pushes to
  the branch start nothing. The first run's lint job was cancelled when a
  newer commit arrived; the rest of that run passed.
- **Platforms:** no macOS, Windows or native ARM64 run happened outside
  the gate.

## Deviations from the spec

None from section 2.

**Changes to the design made while implementing it** (listed in
`docs/hd-composition.md`):
- Kernel lines have their own owner.
- The shown state is kept per pixel, because ScummVM shows rectangles at
  different times.
- Sprites write their priority into the HD priority buffer as they are
  drawn, as `GfxView::draw` does at 1x.
- Saved bits carry the priority-source plane too.

## Open questions

1. **Stopped and always-update views** are part of the background layer,
   so they are hidden with 1x occlusion, as the design says. Is that
   acceptable, or should they also get HD occlusion?
2. **Rooms 290 and 353** draw picture overlays too, but no scenario
   reaches them yet. Their stacks are covered by golden hashes. Should I
   add scenarios that reach them, or leave that to the playthrough of
   milestone 9?
