# Milestone 3b: drawing order, occlusion and stroke integrity

Status: **approved** (2026-10-21). The customer chose option 1 of the
trade-off below: the remaining cases are accepted as listed. The hints are
frozen, and release v0.1 follows. The text below is the report as
reviewed.

Exit criteria (addendum A1, `docs/SPEC_ADDENDA.md`):

- the A1 acceptance checks pass, except cases listed in the report;
- the customer approves the side-by-side review;
- at the gate, the hints are frozen and the golden hashes regenerated;
- release v0.1 is tagged after the gate.

## What was implemented

The decisions are in `docs/ASSUMPTIONS.md`, "Milestone 3b";
`docs/hd-pipeline.md` describes the pass and the checks.

1. **The A1 checks** (`sci::render::check_integrity`): overshoots, depth,
   doubled boundaries, fragments and breaks. They sample the HD scene at
   8 x 8 points per pixel against what path 1 shows. Every case is listed
   with its lines (PIC command indices), a pixel, a size, and for
   overshoots and joints a class (see "Remaining cases").
2. **The stroke integrity pass** in the vector path (visual plane only).
   It works from 1x evidence: which line the player saw at each pixel, and
   which pixels each line drew. In order:
   - **spurs**: stroke points past a line's own end are removed (a fitted
     edge that ran out to a junction and back);
   - **ends on a contour** are moved to the contour's centreline and cut
     along its near edge, or joined to its side by a short cut connector;
   - **joints**: touching line ends are joined by a connector;
   - **runs the player did not see** are removed from the stroke, so a
     boundary drawn twice shows once, in the colour seen at 1x.
   Each change is recorded with its evidence.
3. **Reporting.** `tools/hd-report` adds:
   - the A1 table per picture;
   - the pass's changes counted by evidence;
   - every remaining case with its class and size.
   `--changes <file>` writes all 213,513 changes with their evidence
   (24 MB, not committed). The report for all 88 pictures is
   `reports/milestone-3b-pictures.md`.
4. **Baseline.** The development option `SCI_WITHOUT_STROKE_INTEGRITY`
   builds scenes without the pass. The numbers "before" below come from it.
5. **Drawing order and depth.** The HD scene keeps path 1's order, and the
   depth check finds 15 pixels in the whole game. 1x visibility
   contradicts the priority map at 21 pixels; there the original command
   order is kept (list below). No relations were added, so the AI step was
   not rerun and the hints are unchanged since 3a.
6. **Tests.** Unit tests build small pictures and check the clipped end
   and its cut, a closed corner, a boundary drawn twice, and that the
   checks find overshoots, fragments and split lines. The synthetic golden
   hashes and `tests/golden/pictures.txt` are regenerated, and the same
   hashes hold on ARM64 (qemu-user).

## Results

All 88 pictures of the tested release, with the committed hints.

### The A1 checks

| Check | Without the pass | With the pass |
|---|---|---|
| Overshoots (of 55,971 contacts) | 15,673 | 7,070 |
| Depth (pixels) | 9 | 15 |
| Doubled boundaries (of 53,549 pairs) | 7,349 | 2,859 |
| Fragments | 27,585 | 6,436 |
| Breaks: owned groups split (of 119,751) | 2,229 | 2,958 |
| Breaks: owned groups hidden | 125 | 305 |
| Breaks: joints broken (of 219,614) | 37,595 | 7,396 |

The pass made these changes:

- 108,813 joints closed;
- 47,317 ends clipped;
- 55,539 runs removed;
- 1,844 contacts kept (the end pixel lies on the contour's stroke).

The acceptance criteria of section 6.4 still hold everywhere:

- worst agreement 99.993% (99.987% without the pass);
- 0 swaps;
- 0 line pixels untouched.

The slowest room entry is 487 ms (445 ms without the pass).

### What each step does

Each row turns one step off and keeps the others:

| | Overshoots | Depth | Doubled | Fragments | Split | Hidden | Joints broken |
|---|---|---|---|---|---|---|---|
| All steps | 7,070 | 15 | 2,859 | 6,436 | 2,958 | 305 | 7,396 |
| Without clipping ends | 16,965 | 5 | 2,844 | 3,224 | 1,900 | 110 | 16,209 |
| Without closing joints | 5,885 | 9 | 2,969 | 4,394 | 1,811 | 218 | 28,645 |
| Without removing runs | 7,169 | 18 | 7,135 | 26,788 | 2,997 | 322 | 6,228 |
| Without the pass | 15,673 | 9 | 7,349 | 27,585 | 2,229 | 125 | 37,595 |

### Remaining cases

Every case is in `reports/milestone-3b-pictures.md`, "A1 cases". By class
and size (areas in square pixels, gaps in pixels):

| Class | Cases | ≤ 1/16 | ≤ 1/4 | ≤ 1 | larger |
|---|---|---|---|---|---|
| Overshoot: the end pixel lies past the contour at 1x | 2,526 | 3 | 14 | 1,005 | 1,504 |
| Overshoot: meets the contour at under 30 degrees | 3,445 | 247 | 562 | 2,258 | 378 |
| Overshoot: other | 1,099 | 150 | 253 | 552 | 144 |
| Doubled boundary | 2,859 | 0 | 548 | 2,153 | 158 |
| Fragment | 6,436 | 5,544 | 542 | 293 | 57 |

| Class | Cases | gap ≤ 1/8 | ≤ 1/4 | ≤ 1/2 | larger |
|---|---|---|---|---|---|
| Joint: the end meets another line it touches | 5,370 | 1,117 | 1,157 | 1,636 | 1,460 |
| Joint: alone | 2,026 | 506 | 361 | 585 | 574 |

| Class | Cases | Notes |
|---|---|---|
| Split group | 2,958 | 2,248 in 2 pieces, 710 in 3 or more |
| Hidden group | 305 | |
| Depth | 15 pixels | at line ends |

**Left to the original command order** (1x visibility contradicts the
priority map; a later command wrote a nearer priority without drawing the
visual):

- pic 1: (213, 102), (260, 121), (265, 123);
- pic 3: (55, 10), (67, 31);
- pic 9: (261, 119);
- pic 14: (218, 130);
- pic 75: 14 pixels from (9, 156) to (0, 169), along command 107.

**Left to the fallback:** none. The fallback of fitted chains to their
segments is not implemented (see the trade-off below).

## Trade-off

The pass works on 1 pixel wide, round-ended strokes, and each step trades
one check against another:

- clipping ends removes 9,900 overshoots and 7,600 broken joints, but adds
  3,200 fragments and 1,000 split groups;
- closing joints removes 21,000 broken joints, but adds 2,000 fragments,
  1,200 overshoots and 1,100 split groups. Its connectors and cuts leave
  slivers where later layers cover part of them.

What remains comes mostly from how 1x pixels meet. A vector stroke cannot
reproduce that without breaking another check:

- **The end pixel lies past the contour at 1x** (2,526 overshoots). The
  player saw the line's last pixel beyond the contour's centreline. Cutting
  it removes a pixel the player saw; keeping it counts as an overshoot.
- **Adjacent parallel rows** (3,445 overshoots at under 30 degrees). Two
  lines on neighbouring rows, one fitted half a pixel closer, overlap
  along their length as 1 pixel strokes.
- **An end touching several lines** (5,370 broken joints). The lines it
  touches often lie apart in HD (one hidden behind another), and the end
  meets one of them, not all.
- **Slivers** (5,544 fragments of at most 1/16 pixel: one HD pixel at
  N = 4). These are bits of stroke ends and connectors left between
  other layers.
- **The fallback for fitted chains** would help little: built with no
  fitting at all, the scenes have only 3 to 5% fewer cases. It would also
  need the checks inside scene building (about 0.5 s more per room).

Options:

1. **Accept the listed cases** as the exit criterion allows: the classes
   above, with every case in the pictures report. This is my
   recommendation. The HD rooms look cleaner than before (doubled outlines,
   fragments and gaps fall by 60 to 80%), and the remaining cases are
   small or decided by the 1x picture itself.
2. **Narrow the checks** for the classes where 1x decides: overshoots
   whose end pixel lies past the contour, and joints whose end meets
   another line it touches. Then 19,143 cases remain listed instead of
   27,039.
   This changes how A1 is read, so it is your call.
3. **Local 1x fallback**: draw the 1x pixels around each remaining case as
   blocks. The checks would then pass, but the rooms would get thousands of
   small pixelated spots. I advise against it.
4. **Go on tuning.** I see no step that does not trade one check against
   another, so I cannot estimate what it would gain.

## Side-by-side review

These rooms show 1x, HD without the pass, and HD with the pass. The images
are derived from the game art, so they are posted with the pull request,
not committed:

- 15: walls with windows;
- 22: furniture;
- 80: patterned walls;
- 14: roofs;
- 18: arches;
- 5: round objects.

I can send any other room, or close-ups of listed cases.

## Golden hashes, tests and style

- `tests/golden/pictures.txt` is regenerated, and the same hashes hold on
  ARM64 under qemu-user. The synthetic golden hashes in CI are updated.
- 120 tests pass; clang-format and clang-tidy report no findings.
- The hints are unchanged since 3a. They are frozen when you approve this
  gate (`hints/README.md`).

## Deviations from the spec

None from section 2. Two A1 items are not done, both explained above and
pending your decision:

- the fallback of fitted chains to their segments;
- the rerun of the AI step for relations (none were needed).

## Open questions

1. **The trade-off**: which option, 1 to 4? I recommend 1.
2. **The side-by-side review**: do the rooms look right to you?
3. **The hint spot check**, still open from 3a: the hints are frozen when
   you approve this gate. Do you want to check any pictures first?
4. **Release v0.1**: on approval, I tag it as a GitHub pre-release, as
   agreed at the 3a gate.
