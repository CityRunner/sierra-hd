# Spec addenda

Changes to the task spec that the customer made after its version 15. Each
addendum is recorded as given, with its date. None changes a fixed decision
in spec section 2 unless it says so (A2 does, with the customer's approval).
There is no A3: the customer numbered the third addendum A4.

**Milestone numbers.** Since A4 (2026-10-29) the remaining milestones are
renumbered: old 12 (exact area coverage) became part of the new mandatory
milestone 10, old 10 (ScummVM-compatible saves) is now 11, and old 11
(vectorised font glyphs) is now 12. Numbers in this file use the new
scheme; the mapping is in A4 and in `docs/ASSUMPTIONS.md`, "Milestone
numbers after A4". Where the spec document says "milestone 10", "11" or
"12", read it through that mapping.

## A1: Drawing order, occlusion and stroke integrity, content-aware (2026-10-21)

A1 adds milestone 3b and section 6.10 to the spec. It amends sections 6.4
(acceptance), 6.7 (hint content and freeze) and 12 (milestones). It does
not change any fixed decision in section 2.

### Milestone plan after this change

- **Milestone 3a**: unchanged, except:
  - the hint schema also covers the depth and containment relations of
    section 6.10;
  - the hint files are not frozen at the 3a gate.
- **Milestone 3b** (new, separate gate, right after 3a): implement section
  6.10.
  - The AI step may be rerun during 3b only to add or correct depth and
    containment relations.
  - Exit criteria: the A1 acceptance checks pass except for the cases
    listed in the report, and the customer approves the side-by-side
    review.
  - At the 3b gate the hint files are frozen and golden hashes are
    regenerated.
- **Release v0.1** moves from after milestone 3a to after milestone 3b.
- Milestones 4 to 9 and the optional milestones after milestone 9 are
  unchanged. (When A1 was given these were 10 to 12; since A4 they are 11
  and 12, and old 12 became part of milestone 10.)
- If the agent is already past milestone 3a when it reads this, milestone 3b
  becomes the next gate: finish the current milestone, report, and propose
  3b before continuing. Do not reopen approved milestones.
- If something in the current design makes A1 impractical or forces a
  trade-off (for example with the acceptance metric), stop and explain it in
  the report instead of improvising.
- The 3b report lists what was implemented, the results of every A1 check,
  and the cases left to the original command order or to the fallback.

### Section 6.10: Drawing order, occlusion and stroke integrity (content-aware)

In the original, later PIC commands simply overwrite earlier ones at 1x, and
at that resolution small defects rarely show. In HD, wider strokes,
antialiasing and fitted curves make every ordering mistake, every leftover
fragment and every break in a line visible. The HD scene must therefore
follow what is in front of what and keep lines whole, not merely replay the
commands.

**Rules: drawing order and occlusion.**

- Contours of different objects meet without overlapping. A line that ends
  on another object's contour (for example a wallpaper line that ends at a
  window frame) is clipped at that contour: it does not cross it, and its
  end does not overlap it.
- Nearer objects are drawn over farther ones. A farther object's lines or
  fills never cover a nearer object's contour or interior.
- Equal neighbours (adjacent objects at the same depth, neither containing
  the other) share their common boundary: it is drawn once, as one stroke,
  not as two overlapping strokes. If the two contours have different
  colours, the boundary takes the colour visible at 1x.
- An object may be drawn over another only with a justification from the
  evidence below; without one, these rules apply.

**Rules: stroke integrity.**

- **No stray fragments.** Nothing may remain in HD that has no meaning in
  the scene. When a chain of edges is replaced by a fitted curve, or an
  outline is clipped, merged or reordered, no leftover of the old geometry
  stays behind as an isolated pixel, dot, spur or short segment.
  - Example: when an arc is smoothed, a corner that was part of the
    original stepped chain must become part of the curve, not an extra
    detached pixel next to it.
  - Every original line pixel is either covered by the primitive that
    replaces it or explained by a neighbouring object.
- **No missing parts.** Nothing that is continuous in the original may
  break in HD. Strokes that are connected at 1x (8-connected line pixels)
  stay connected in HD, with no gap at joints, corners, line ends or curve
  transitions.
  - Example: the lines of a roof must meet without a gap in HD, even where
    the fill behind them is already correct; a correct fill does not excuse
    a broken outline.
- Strokes that are separate at 1x stay separate in HD, unless a gap is
  closed under the gap rule of section 6.3.
- Where a fitted primitive cannot satisfy these rules within the deviation
  threshold, that chain falls back to its original segments (section 6.4).

**Evidence**, in this order of precedence:

1. 1x visibility and connectivity: which object's pixels the player
   actually saw at a contact or crossing, and which line pixels are
   connected, read from the path-1 maps.
2. Depth: the path-1 priority map (SCI priority bands encode depth), and the
   depth and containment relations from the AI hints ("in front of",
   "behind" and "contains" between regions and chains).
3. The original command order, as the tie-breaker.

If the evidence contradicts itself, or 1x visibility contradicts depth with
no hint to resolve it, keep the original command order and list the case in
the replacement report.

**Limits.**

- These rules change only the HD picture. The path-1 visual, priority and
  control maps, game logic and original mode are never affected.
- After reduction to 1x, any pixels that differ because of these rules must
  lie on the involved strokes, within the 1 px edge band of section 6.4. No
  region may change its pair.
- Every clip, reorder, merged boundary, removed fragment and closed joint
  appears in the replacement report with the evidence that justified it.

**Acceptance (milestone 3b).** Automated checks over every picture find
none of the following, except the cases listed in the report:

- a stroke overshooting a contour it ends on;
- a farther object covering a nearer one according to the priority map;
- a doubled boundary between equal neighbours;
- an isolated fragment in HD that does not correspond to a feature visible
  at 1x;
- a break in HD between line pixels that are connected at 1x (the connected
  components of every stroke match between 1x and HD).

The customer's side-by-side review includes rooms with dense overlapping
detail and many curves, such as walls with windows, furniture in front of
patterned walls, roofs, arches and round objects.

## A2: Sprite undithering (2026-10-21)

**Withdrawn (2026-10-28).** The customer dropped A2 and milestone SU
completely: sections 1.1, 2, 6.5 and 12 apply as before A2, sprites stay
as in the original scaled N x N, and the milestone 9 playthrough is done
without undithering. The text below is kept as a record only.

A2 adds milestone SU and section 6.11. It amends section 1.1 and the output
decision in section 2 (a fixed decision: **changed with the customer's
approval**, given with this addendum), and sections 6.5 (shared colour
blending) and 12 (milestones).

### Milestone plan after this change

- **Milestone SU** (sprite undithering): a separate gate between milestones
  6 and 7.
  - It starts after milestone 6 (6b) is approved, implements A2, reports,
    and waits for approval before milestone 7 starts.
  - The numbering of milestones 7 to 12 does not change.
  - Approved milestones are not reopened.
  - If something in the design makes A2 impractical, the agent stops and
    explains it in a report instead of improvising.
- **Milestone 9**: the playthrough in HD mode is done with sprite
  undithering on.
- Milestone 4 continues unchanged; A2 is recorded without interrupting it.

### Amendment to sections 1.1 and 2

Sprites keep their original shape and pixels and are still scaled in N x N
blocks with no added detail, but in HD mode their dithered areas are shown
as computed colours, like the backgrounds.

### Section 6.11: Sprite undithering

Difference from backgrounds: in PIC resources a dithered area is stored as a
colour pair, so the pair is known. In views the dithering is baked into the
cel as a checkerboard of two palette colours, so the pairs must be detected
in the raster.

**Rules.**

- **Detection:** a dithered area is a connected region of a cel where
  exactly two opaque colours alternate in a checkerboard pattern (by parity
  of x + y in cel coordinates). A region qualifies only above a minimum size
  and with no dimension below 2 pixels, so that intentional fine detail
  (eyes, outlines, highlights, single-pixel lines) is kept. The thresholds
  are tuned on the game's cels at this milestone and recorded in
  `docs/ASSUMPTIONS.md`.
- **Colour:** every pixel of a qualifying region gets the blended colour of
  its pair from the same 256-entry LUT and the same rules as the backgrounds
  (section 6.5), so sprites and backgrounds match exactly.
- **Transparency:** patterns mixing a colour with the transparent key are
  left as they are (no alpha blending). The cel's transparency mask is never
  changed, so sprite shapes, collisions and priority occlusion stay exactly
  as in the original.
- **Scaling:** undithering happens on the 1x cel; the result is then scaled
  in N x N blocks as before. No smoothing at region edges.
- **Scope:** every cel drawn in HD mode, including static views added to the
  picture (addToPic). Fonts and cursor resources are not affected.
- **Mirrored cels:** detection runs on the stored cel, and the result is
  mirrored with it.
- **Original mode:** off; sprites look exactly as in the original. In HD
  mode the feature is on by default and can be switched off in the
  configuration.
- **Ambiguous cases:** an override file per view resource (in
  `hints/views/`, with a schema validated in CI) may exclude a region or
  force one, anchored to view, loop, cel and 1x coordinates and to a hash of
  the view resource. The agent may use AI at development time to review the
  detections; the overrides are frozen at the SU gate, like the picture
  hints. A mismatch with another release's resource skips the override and
  logs it.
- **Determinism:** integer arithmetic only; the results are computed once
  per cel and cached together with the picture analysis (section 10.2).
- **Reference:** check whether ScummVM at the pinned revision has its own
  view undithering, and if so compare its detections with ours and document
  the differences.

**Acceptance (milestone SU).**

- Unit tests on synthetic cels: checkerboards of every colour pair are
  detected; fine detail, transparency patterns and regions below the
  thresholds are not.
- A report listing, for every view, the undithered regions and the
  overrides used, with side-by-side images (original and HD) of all
  characters, captured both as isolated cels and in game scenes, and of the
  views the customer chooses; images stay in the git-ignored
  `reports/images/` directory.
- The game logic is unaffected: the 1x priority and control maps and the
  cel transparency masks are bit-identical with and without undithering,
  and the scenario recordings of milestones 4 to 6 still give matching
  traces.
- Golden hashes of sprite rendering are regenerated once at the gate.

## A4: HD background quality and the visual review format (2026-10-29)

A4 adds the mandatory milestone 10 "HD background quality", which replaces
and extends the former optional milestone 12, renumbers the remaining
milestones, and introduces a visual review format for the operator (the
customer). It amends spec sections 6.3, 6.4, 6.6, 6.7, 6.9, 9 and 12. It
does not change any fixed decision in spec section 2. Milestone 9 is
finished as planned; its scope and exit criteria do not change.

### Milestone plan after this change

| Old | New | Content | Status |
|---|---|---|---|
| 12 (optional: exact area coverage) | 10 | HD background quality: object-level reconstruction (part A below), mandatory; exact area coverage (spec section 6.6) as an optional part, which the customer approves or declines at the start of milestone 10 | mandatory, next after 9 |
| 10 | 11 | ScummVM-compatible saves (spec section 9) | optional: approved or declined after milestone 10 |
| 11 | 12 | Vectorised font glyphs (spec section 6.9) | optional: approved or declined after milestone 11 |
| — | — | Audit and iteration 2 | last, unchanged |

- Completed milestones keep their numbers, and their reports are not
  changed.
- The new numbers apply everywhere from now on. Where the spec document says
  "milestone 10", "11" or "12", it is read through this mapping.
- In milestone 10, the picture hint files (frozen at milestone 3b) are
  unfrozen at the start and frozen again at its gate. Golden hashes are
  regenerated once at the milestone 10 gate, for all changes of the
  milestone.
- The review format of part B is used for every visual review from now on
  (the milestone 9 report if it includes picture reviews, milestone 10, the
  audit and iteration 2, and any re-review).
- If something in the design makes A4 impractical, the agent stops and
  explains it in a report instead of improvising.

### Part A: object-level reconstruction of HD backgrounds (PIC only)

Goal: reconstruct whole objects, not separate lines: understand what a
picture shows, which parts each object consists of and which primitives draw
them, and reproduce the shapes in HD without artefacts or wrongly guessed
shapes. Sprites, fonts and the cursor are not affected.

**A.1 Scene model.** Every picture is described as a hierarchy: object, its
parts, their primitives (for example: window; frame, glazing bars, sill;
lines and corners). Everything is anchored to PIC command indices and 1x
coordinates and stored in the hint files with an extended schema (spec
section 6.7), validated in CI.

**A.2 Shape vocabulary.** Typical objects with templates (window, door,
arch, staircase, column, roof, furniture and others found in the game) and
their components: straight lines, arcs, ellipses, splines, sharp and rounded
corners, intentional steps, repetitions.

**A.3 Joint reconstruction.** Geometric relations inside an object
(parallelism, perpendicularity, equal spacing, symmetry, tangent transition
between an arc and a line, alignment) are solved together for the whole
object, not line by line.

**A.4 Intended versus raster artefacts.**
- Real steps, bricks and crenellations are kept; raster staircasing is
  smoothed.
- An object's real corner stays sharp; a raster kink is smoothed.
- Organic shapes (trees, bushes, rocks, clouds) get no geometric
  idealisation (no symmetry, no straightening), only careful smoothing.

**A.5 Repetitions.** Identical elements within a room (windows in a row,
balusters, steps) are reconstructed identically.

**A.6 Protection against wrong guesses.**
- Tolerance: a reconstructed shape deviates from the original pixels by no
  more than about 1 original pixel.
- Round-trip check: the reconstruction, rasterised back to 1x with the
  path-1 algorithms, matches the original pixels within the tolerance;
  otherwise the hypothesis is rejected.
- Several hypotheses per object, each with a confidence score.
- Never invent: no detail that is not present in the original pixels, even
  if the object type suggests it.

**A.7 Uncertain cases are decided by the operator (the customer).**
- Uncertain recognitions and close hypotheses are collected in batches by
  group of rooms, as review sheets in the format of part B, with the
  alternatives shown side by side; the customer chooses.
- While the customer answers, the agent continues with other rooms. Until a
  decision, the current reconstruction (milestone 3b) stays in that place.
- The customer's decisions are written into the hint files as manual edits,
  with the cell reference, and are never asked again.

**A.8 Compatibility.** The result obeys addendum A1 (drawing order,
occlusion, stroke integrity) and the metric and line check of spec section
6.4.

**A.9 Limits.** Only the HD picture changes. Game logic, the path-1 maps,
original mode and saves are not affected. Determinism is kept; AI runs
offline and only during development.

**A.10 Acceptance (milestone 10).**
- A report per room: recognised objects, parts, primitives, relations,
  confidence, deviation from the original pixels.
- The round-trip check passes for every accepted reconstruction.
- All uncertain cases are decided by the customer.
- A side-by-side review of the rooms in the format of part B.
- The metric of section 6.4 and the checks of addendum A1 pass.
- The scenario recordings of milestones 4 to 9 still give matching traces
  (the game logic is unaffected).
- If exact area coverage was approved: the differential tests against the
  16x16 supersampling pass, as described in spec section 6.6.

### Part B: visual review format for the operator

**B.1 Overview sheet**, one per picture under review:
- Two images of the same size, HD on top and original below, at the same N
  (default N = 4: each image 1280 x 800 including the menu bar row).
- The same grid on both, drawn over the picture area only (320 x 190
  original pixels, below the 10-row menu bar):
  - 5 columns numbered 1 to 5 from left to right, boundaries at original
    x = 0, 64, 128, 192, 256, 320;
  - 3 rows lettered A to C from top to bottom, boundaries at original
    y = 0, 63, 127, 190 within the picture area;
  - each cell is split into quarters: .1 top left, .2 top right, .3 bottom
    left, .4 bottom right (for example B3.2); quarter boundaries are at the
    rounded midpoints of the cell, on whole original pixels.
- Cell lines are thin, semi-transparent and outlined in a contrasting
  colour so they stay visible on light and dark areas. Quarter lines are
  thinner and dashed. Only cells are labelled (for example "B3"), with a
  small label on a backing in the cell's top-left corner; quarters are not
  labelled.
- The same pair without the grid, as a separate image.
- A header with the room and picture numbers, the sheet version and the
  date.

**B.2** Questions in a batch sheet refer to cells or quarters, for example
"Case 7: B3.2, window frame, alternatives a / b", with enlarged crops of the
alternatives next to it.

**B.3** The customer's answers name a room and a cell, a quarter, several
cells or a range, for example "room 12, B3: ...", "B3.2: ...",
"A2-A4: ...".

**B.4** For every cell or quarter the customer names, the agent prepares an
enlarged crop (the area plus half a cell around it, at a larger N, HD and
original, with the grid) and, where relevant, alternatives for the
correction. Decisions go into the hint files with the cell reference.

**B.5** All sheets are stored in the git-ignored `reports/images/`
directory, like every image derived from the game art.
