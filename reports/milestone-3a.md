# Milestone 3a: scene-analysis hints and the room viewer

Status: **ready for review**. The hints are committed and wait for your
spot check.

Exit criteria (spec section 12, amended by addendum A1 in
`docs/SPEC_ADDENDA.md`):

- AI-assisted scene analysis (spec section 6.7) integrated;
- hint files committed to `hints/`. Under A1 they are frozen at the
  milestone 3b gate, not here, and the schema also covers the depth and
  containment relations of section 6.10;
- golden hashes regenerated;
- the room viewer of release v0.1 (rooms, sprites, fonts; window, choice of
  N, original/HD switch), without the debug overlays, with CI binaries.
  Under A1 the release itself is tagged after milestone 3b.

## What was done

The decisions are in `docs/ASSUMPTIONS.md`, "Milestone 3a".

1. **Hint format and validation.**
   - `hints/README.md` describes the format; `hints/schema.json` is the
     committed schema.
   - CI checks every hint file against the schema with
     `scripts/check-hints.py`, which needs no dependencies.
   - The program reads the files with a strict reader
     (`sci::vector::parse_hints`, on the new JSON reader in sci-core).
     Both the JSON reader and the hint reader have fuzz targets.
   - Hints name lines by their PIC command index, never by coordinates, so
     they also hold for mirrored drawings. Each file carries the SHA-256 of
     its PIC resource; on other game data it is skipped and logged.
2. **Hints in the vector path** (`docs/hd-pipeline.md`, "Hints").
   - A **chain hint** is fitted by the requested kind (line, arc, spline,
     ellipse) within 1 pixel, through every point where other lines meet
     the chain. It is kept only if the faces stay the same and every pixel
     its lines own at 1x stays touched by the stroke. Otherwise the
     automatic result stands.
   - A **gap hint** closes a gap of at most 8 pixels, and is kept only if
     mixed faces and 1x-block pixels do not increase.
   - The replacement report lists every hint used, with what it changed,
     and every hint left out, with the reason.
3. **The analysis** (the AI step).
   - `tools/hint-tool` proposes the chains that may be curves the
     automatic fitting missed, and tries every kind on each.
   - Claude reviewed the candidates on images of every picture, as four
     agents with the same written brief working in parallel, and judged
     each one: a curve of the drawn object (arch, drape, tree, figure,
     rock), or a shape meant to be angular (roof, stairs, frame, crack,
     zigzag hedge). For each accepted candidate the review chose the kind
     and wrote a label, and it added region labels and symmetry.
   - Gap hints were searched only at the faces that still fell back to 1x
     blocks.
   - The result is 86 hint files:
     - 1709 chains, 783 of them marked uncertain;
     - 5 gaps, all uncertain;
     - 508 region labels.
   - The files hold labels and line numbers only, no image data.
4. **The window: release v0.1** (`src/app`).
   - `sierra-hd` is the room viewer in an SDL3 window:
     - every picture in HD or original mode, with the visual or priority
       map;
     - every view with its animation, and every font;
     - N automatic (the largest that fits in physical pixels, letterboxed)
       or chosen with + and -;
     - fullscreen, and a status line in the game's own font.
   - Only changed rectangles go to the texture.
   - On start, it:
     - reads `sierra-hd.ini` next to the executable;
     - asks for the game folder the first time;
     - identifies the release by its hashes;
     - loads the hints from `hints/` next to the executable.
   - There are no debug overlays in this build.
   - New in app: a `sci-platform` library (threads, game files,
     configuration, identification, hint loading), which the tools now
     share. MD5 was added to sci-core for ScummVM's detection of the 5.25"
     release.
5. **Release workflow** (`.github/workflows/release.yml`).
   - It builds, tests and packages Linux and Windows, both on x64 and
     ARM64; macOS only on a manual run.
   - Pull requests get the packages as artifacts.
   - Pushing the tag `v0.1.0` publishes them as a GitHub pre-release with
     `SHA256SUMS`. Under A1 that happens after the 3b gate.
   - A package holds:
     - the program;
     - `hints/`;
     - the user README (`docs/USER-README.md`);
     - the licences.

     It never holds game data.
6. **SDL 3.4.18** is vendored (`third_party/SDL3`, `THIRD_PARTY.md`).

## Results

### Pictures with the hints (all 88, tested release)

The per-picture table and the full lists of hints used and left out are in
`reports/milestone-3a-pictures.md`.

| What | Milestone 3 | Milestone 3a (with hints) |
|---|---|---|
| PIC lines replaced by fitted curves (visual plane) | 20,321 | 24,225 |
| Fitted chains: straight / arcs / splines | 313 / 712 / 11,776 | 226 / 551 / 12,427 |
| Mixed faces (visual plane) | 5 | 1 (picture 47) |
| Pixels drawn from 1x blocks | 648 | 8 |
| Chain hints used, visual plane | n/a | 1709 of 1709 |
| Chain hints used, priority plane | n/a | 304; 57 left out (the curve would miss priority pixels) |
| Gap hints used | n/a | 5 of 5 |
| Worst agreement outside the edge band (target 99%) | 99.987% | 99.987% |
| Swaps / untouched line pixels (target 0) | 0 / 0 | 0 / 0 |
| Room entry at N = 10, slowest / median | 530 / 271 ms | 476 / 285 ms |

- Every picture passes the acceptance check with the hints.
- Hint chains often join chains that the automatic fitting had cut into
  arcs, so arcs drop and splines grow.
- The effect is deliberately conservative: outlines get rounder and
  smoother. The hints add no new content, and no shape moves more than 1
  pixel.

### Golden hashes

- `tests/golden/pictures.txt` is regenerated with the committed hints
  (`--hints hints`).
- The HD hashes of 80 pictures changed: those with hints, at N = 1, 2, 5
  and 10.
- No original-mode hash changed: original mode is still bit-identical to
  milestone 2.
- **ARM64**: the cross build under qemu-user reproduces all 440 hashes
  with the hints.

### Unit tests, fuzzing and style

| Check | Result |
|---|---|
| clang-format, repository policy, hint schema, clang-tidy | pass, no findings |
| Linux x86-64 debug and release | 17/17 tests pass |
| Linux x86-64 ASan+UBSan | 17/17 tests pass, no sanitizer reports |
| libFuzzer, 60 s each, new targets | `fuzz-json` 1.97 M runs, `fuzz-hints` 2.13 M runs; no crashes, hangs, leaks or out-of-memory |
| Hint-anchor check (`sci-hint-tool check hints`, with the game) | 86 files pass |

New tests:

- the JSON reader;
- the hint reader (valid and broken files);
- hints in the scene: chains fitted as asked, the line check, unknown
  commands, gaps kept and rejected;
- forced fits, including a nearly straight arc;
- MD5 against RFC 1321;
- the configuration file;
- rejection of unknown game folders.

### The program

- It starts on the tested release and identifies it. An unknown folder
  stops with a message. It quits cleanly and writes its configuration.
- The release package for Linux x64 was unpacked and run on its own: it
  found its hints and identified the game. This ran under SDL's offscreen
  driver, since the container has no display.
- Frames from the viewer were checked as images: HD and original
  pictures, the priority map, a view over the room, the font sheet and the
  status line.
- **Not tried yet on a real display, or on Windows.** The Windows build
  and tests run in CI on this pull request. Please try the binaries from
  the pull request's artifacts.

## Addendum A1 (milestone 3b)

You added milestone 3b on 2026-10-21. It is recorded in
`docs/SPEC_ADDENDA.md` and `docs/ASSUMPTIONS.md`. In this pull request:

- the hint format has `relations` ("in-front-of", "behind", "contains"
  between regions and chains), checked by the reader, the schema and CI.
  The committed files have none yet: 3b adds them by rerunning the AI step
  for that purpose only;
- the hint files are not frozen at this gate;
- `v0.1.0` is not tagged after this gate.

**Points in A1 to settle before or early in 3b.** You asked me to raise
trade-offs instead of improvising. I see three, all about how the checks
are measured:

1. **8-connected line pixels.** At 1x, separate lines often touch
   diagonally along their whole length: hatching, wood grain and staircase
   steps. The player sees them as separate lines. If "connected at 1x"
   includes such contacts, the HD lines would have to be joined into
   blobs. **Proposal**: count lines as connected where a line's end or
   corner pixel touches another line's pixels (joints, corners, line ends,
   curve transitions, as A1 lists). Side-by-side contact along the lines
   would stay separate.
2. **Doubled boundaries.** Two outlines drawn on the same pixels are a true
   double, and 3b draws them once. Two outlines drawn on neighbouring
   pixel rows were seen by the player as a 2-pixel border, and merging
   them would change the 1x picture. **Proposal**: treat only coincident
   or overlapping outlines as doubled.
3. **"Covering" at stroke width.** Every 1-pixel stroke reaches half a
   pixel into the face beside it, at any depth. **Proposal**: count as
   covering only the HD samples outside the stroke's own 1x pixels and
   their edge band (the band of section 6.4).

Expected work in 3b:

- clipping line ends at contours;
- depth order from the priority map and the relations;
- merged boundaries;
- removing fragments and closing joints;
- five new automated checks;
- the report of every change and its evidence.

I expect the main source of stray fragments to be the thin faces (slivers
without a pixel centre of their own) that are drawn as 1x blocks. 3b will
measure that first.

## Deviations from the spec

- None from section 2.
- **The analysis cache is deferred to milestone 4** (spec section 10.2).
  Without the VM the viewer builds a picture's scene when the picture is
  shown (at most 0.48 s at N = 10, within the room-entry budget) and keeps
  the last four. The cache on disk and the first-launch analysis belong to
  entering rooms, which the VM brings. **Please confirm.**
- **The macOS release binary** is built only on a manual run, as agreed
  for CI.
- **Linux binaries need glibc 2.38 or newer** (Ubuntu 24.04, Debian 13,
  Fedora 39). The C++ runtime is linked in.

## Open questions

1. **Hint spot check (spec section 6.7).**
   - Which pictures would you like to check? I can send, per picture, the
     image with every hint drawn and labelled, and the HD result with and
     without hints.
   - 788 hints are marked uncertain, too many to check one by one. The
     largest groups:
     - outdoor ground, lawn and shore edges (pictures 24 to 30);
     - curves in window panes, which may be curtains or reflections
       (pictures 10, 12, 15 to 17 and 20);
     - lines on the crypt wall, marble veins or cracks (picture 57);
     - the dark shapes in pictures 39, 40, 46 and 48.

     Rejecting a doubtful group costs little: those lines keep the
     automatic result.
   - One known inconsistency: pictures 182 and 382 draw the same collar
     line. It is a spline in 182 and a straight line in 382, the only kind
     the tool accepted there.
2. **The analysis cache** (see Deviations): deferring it to milestone 4 is
   fine?
3. **Release**: after the 3b gate I tag `v0.1.0`, and the release
   workflow publishes the pre-release. Is a pre-release on GitHub what you
   want?
4. **A1 measurements** (see "Addendum A1"): do you agree with the three
   proposals for connectivity, doubled boundaries and covering?
