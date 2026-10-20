# Milestone 3: HD picture pipeline

Status: **ready for review**. All exit criteria are met. The manual
acceptance (side-by-side images of rooms chosen by the customer) is part
of this gate; see "Open questions".

Exit criteria (spec section 12):

- original mode is bit-identical to milestone 2;
- a per-room report of replacements (arcs, lines) and fallbacks, checked
  against the acceptance criteria of section 6.4;
- golden hashes of the HD view recorded as the regression baseline.

## What was done

The pipeline is described in `docs/hd-pipeline.md`; the decisions are in
`docs/ASSUMPTIONS.md`, "Milestone 3".

1. **Path 1 trace** (sci-pic). `draw_picture` can record every drawn
   primitive with its state and, per pixel, which one wrote the visual and
   priority maps last. Path 1's own output is unchanged (899 of 899 dumps
   still match ScummVM).
2. **sci-core foundations**: checked int64 arithmetic, rationals and the
   orientation predicate, explicit rounding, SHA-256, and the task-execution
   interface.
3. **sci-vector**, the vector analysis, per plane (visual and priority):
   - an exact planar graph of the lines (int64 rationals in doubled pixel
     coordinates);
   - gaps closed exactly where path 1's line pixels touch, so fills are
     separated as at 1x, and leaks stay as the player saw them;
   - faces coloured from the path-1 map, with a fallback to 1x blocks where a
     face's pixels disagree or where it has none;
   - curve fitting for chains of short lines: lines, arcs, cubic splines and
     ellipses, smooth across junctions, within 0.5 pixels and never changing
     the faces;
   - strokes, brushes and textured blocks as layers in drawing order.
4. **sci-render**:
   - 16 x 16 supersampling with the face partition, integer colour
     averaging in 16-bit sRGB, and rows in parallel;
   - the HD priority map and original mode;
   - the acceptance check, and a simple reference sampler for differential
     tests.
5. **Tools**:
   - `tools/hd-report`: the per-picture replacement report, acceptance check
     and room-entry time, optionally with side-by-side images;
   - `tools/golden-hashes`: writes and checks the golden hashes;
   - the room viewer renders HD frames and priority maps (`--hd`);
   - the CMake option `SCI_DEBUG_HIGHLIGHT` highlights fitted strokes and
     fallbacks (spec section 6.3).
6. **Docs**:
   - new: `docs/hd-pipeline.md`;
   - updated: `docs/ASSUMPTIONS.md`, `docs/ARCHITECTURE.md`, the READMEs
     of sci-core, sci-vector and sci-render, the tool READMEs,
     `tests/README.md`, `THIRD_PARTY.md` and the README.

## Test results

### The HD pipeline on all 88 pictures (local, game files of the tested release)

The per-picture table is `reports/milestone-3-pictures.md`. It holds
numbers only, no images. In total:

| What | Result |
|---|---|
| PIC lines drawn as strokes | 94,327 of 96,502 (the rest are completely overdrawn) |
| Gaps closed (extensions / joins / perpendicular) | 64,302 / 176,593 / 29,073 |
| Faces | 53,747: 38,802 uniform, 14,940 thin (slivers), 5 mixed |
| Region pixels drawn from 1x blocks (mixed faces) | 648 of 5.35 million (0.012%) |
| Fitted chains | 313 lines, 712 arcs, 11,776 splines, 0 ellipses; 20,321 PIC lines replaced |
| Brushes | 3,282 solid (shapes), 220 textured (blocks) |
| Agreement outside the edge band (target at least 99%) | worst picture 99.987%; 9 differing pixels in the whole game |
| Swaps (target none) | 0 |
| Line pixels not touched by their stroke (target none) | 0 of 1,050,137 |
| Line pixels covered by later layers (drawing order) | 158 |
| Mixed faces of the priority plane | 99 |

- All 88 pictures pass the acceptance check, under ASan+UBSan as well
  (no sanitizer reports).
- The report lists every mixed face with its bounding box, 5 in all. They
  lie in pictures 5, 15, 47 and 78.
- No closed chain fitted an ellipse within 0.5 pixels: the game's round
  shapes are drawn either as brushes or as polygons too coarse for an
  ellipse at that tolerance. They are fitted as splines instead.

### Original mode and golden hashes

- **Original mode** at N = 1 is identical to ScummVM's visual map for all
  88 pictures (checked against the milestone 2 reference dumps).
- **Golden hashes** of every picture are in `tests/golden/pictures.txt`
  (440 hashes): the HD frame at N = 1, 2, 5 and 10, and the original frame.
  The ARM64 cross build under qemu-user reproduces all 440.
- **Synthetic golden hashes** (`tests/golden/synthetic_test.cpp`) cover a
  normal and a mirrored picture. They pass on x86-64 and on ARM64 under
  qemu, and CI runs them on every platform.

### Room entry and analysis time (development container, 4 x86-64 cores)

| | N = 5 | N = 10 |
|---|---|---|
| Room entry, slowest picture | 258 ms | 530 ms |
| Room entry, median | 124 ms | 271 ms |

- Room entry is path 1 plus both scenes plus the HD frame and priority map.
- Building the scenes of all 88 pictures takes 4.5 s on one thread. That is
  what the first launch analyses before caching (spec section 10.2).
- Rendering was optimised after measuring: picture 128 at N = 10 dropped
  from 5.1 s to 0.92 s on one thread, with bit-identical output.

### Unit tests, fuzzing and style (container; Linux CI on every push)

| Check | Result |
|---|---|
| clang-format, repository policy, clang-tidy | pass, no findings |
| Linux x86-64 debug, release, ASan+UBSan | 14/14 tests pass in each |
| Linux ARM64 (cross build under qemu) | 14/14 tests pass, golden hashes included |
| libFuzzer, 60 s per target | `fuzz-byte-reader` 2.6 M runs, `fuzz-decompress` 0.51 M, `fuzz-game-folder` 0.86 M, `fuzz-pic` 15 k, `fuzz-view-font` 0.42 M, `fuzz-vector` (new) 858; no crashes, hangs, leaks or out-of-memory |

New tests:

- sci-core: rationals, the orientation predicate, rounding, SHA-256 (FIPS
  vectors) and the executor.
- Path 1: the trace (primitives, owners, mirroring, overlays).
- sci-vector:
  - faces, gap closing, leaks, the border, dots, hidden lines, brushes, the
    priority plane, mirroring, and the face partition;
  - curve fitting: lines, arcs, ellipses, pinned points and the tolerance;
  - a whole circle of short lines;
  - the differential test of the sampler against the reference.
- sci-render: colours, coverage, layer order, fallbacks, task-order
  independence, and original mode.

The CI results are on the pull request: Linux on every push, Windows on the
pull request, macOS only on request.

ASan found one bug while this milestone was being written. The arrangement
took `std::minmax` of temporaries, which returns dangling references. It is
fixed; outputs and golden hashes did not change.

## Deviations from the spec

- None from section 2.
- **Hints, the window and release v0.1** belong to milestone 3a (the next
  gate). The room viewer still writes PNG files.
- **Overlays are not yet in the golden hashes.** The spec asks for the
  overlays "where a room uses them", and only the scripts tell which those
  are. They are added at milestone 4; the pipeline already handles overlays.
- **The switch that snaps the HD priority map to the sprite grid**
  (spec section 6.6) comes with the sprites at milestone 5. At this
  milestone the HD priority map has smooth edges, which is the spec's
  default.
- **Kernel drawing primitives and static views in the picture** (spec
  section 6.8) need the VM; they are milestone 5.
- **Fits use 0.5 pixels**, the lower end of the spec's "about 0.5 to 1 px".
  At that tolerance the line check holds (every line pixel stays touched by
  its stroke). A wider tolerance gives rounder curves but breaks it for some
  pixels.

## Open questions

1. **Manual acceptance (spec section 6.4).** Which rooms would you like to
   see side by side, original and HD? Side-by-side images of all 88
   pictures are generated (git-ignored, never committed), and samples are
   attached to the chat summary.
2. **Fitting tolerance (spec section 15).** Keep 0.5 pixels, or allow up to
   1 pixel for rounder curves? The cost is that a few line pixels would no
   longer be touched by their stroke.
3. **Edge blend space (spec section 15).** HD edges and dither pairs are
   blended in sRGB, as assumed. Please confirm once you have seen the
   images.
4. **Reference machine and budgets (spec section 10.3).** The proposal, in
   `docs/ASSUMPTIONS.md`:
   - a laptop with a 4-core x86-64 CPU of about 2020, 8 GB of memory and
     integrated graphics;
   - room entry under 1 s at N = 10;
   - first-launch analysis under 15 s.
