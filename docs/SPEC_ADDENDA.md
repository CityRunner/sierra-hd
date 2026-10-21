# Spec addenda

Changes to the task spec that the customer made after its version 15. Each
addendum is recorded as given, with its date. None changes a fixed decision
in spec section 2 unless it says so.

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
- Milestones 4 to 9 and the optional milestones 10 to 12 are unchanged.
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
