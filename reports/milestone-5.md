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
   ATTIC_LINE
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
5. GOLDEN_LINE

## Results

### Scenarios: traces, screens, 1x maps and actor positions

Every scenario of `tests/scenarios/scenarios.txt` was run in the reference
and in sci-vm and compared on all four counts. Everything matched:

SCENARIO_TABLE

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

CI_SECTION

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
2. **Room 290's overlay:** see "Golden hashes" above.
