# The HD picture pipeline (path 2)

How a PIC resource becomes an HD background (spec sections 6.2 to 6.6).
Path 1 (`sci-pic`) draws the picture at 1x exactly as ScummVM does; path 2
(`sci-vector`, then `sci-render`) turns what path 1 drew into a vector
scene and rasterises it at 320N x 190N. Game logic only ever reads path 1's
maps.

## 1. The trace of path 1

`draw_picture` can record a `DrawTrace`: every line, fill and pattern with
the state it was drawn with (planes, colour pair, priority, control), after
mirroring and in picture coordinates, and for every pixel of the visual and
priority maps the primitive that wrote it last (`visual_owner`,
`priority_owner`). Recording does not change what path 1 draws.

Everything below works per plane: the visual plane (values are EGA colour
pairs) and the priority plane (values 0-15).

## 2. The planar graph

`src/sci-vector/src/topology.cpp` builds an exact arrangement of the
plane's lines.

- **Coordinates** are doubled picture coordinates, so that pixel centres,
  where PIC lines start and end, are integers: pixel (x, y) has its centre
  at (2x + 1, 2y + 1). Line end points are clipped to the screen as path 1
  clips them. The picture frame is the rectangle (0, 0)-(640, 380).
- **Arithmetic** is exact, in `int64_t`: intersections are rational points
  with denominators below 2^24; every product stays below 2^60 and is
  checked (`sci/core/exact.hpp`).
- **Segments** are the plane's lines (dots, lines of one pixel, have none),
  the connectors of section 3, and the frame. All intersections become
  vertices; overlapping segments share edges.
- **Faces** are the cycles of half-edges, the face lying to the right of
  each half-edge on screen. A connected component inside a face gets its
  own outer cycle as a face; no point location is needed, because every face
  takes its value from the pixels it contains (section 4).
- **Labels**: every pixel centre gets its face by an exact scanline walk;
  centres on an edge are marked as such.

## 3. Closing gaps as path 1 closes them

A fill spreads to the four neighbours of a pixel, so two lines whose pixels
touch, even diagonally, stop it, whether or not the lines meet
geometrically. For every line end, the lines whose pixels touch the line's
own pixels within 2 pixels of that end are connected to it, unless the lines
already meet:

1. **Extension**: the line is extended along its own direction to the other
   line, if it hits it within 2 pixels.
2. **Join**: otherwise the end is joined to the other line's nearer end
   point, if that lies within 2 pixels.
3. **Perpendicular**: otherwise a connector runs from the end, square to
   the other line, to its foot on that line, if that lies within 2 pixels.

Dots connect to the lines and dots next to them. A line end in the
outermost pixel row or column connects to the frame, which lies half a
pixel further out. Connectors only close the graph; they are not drawn.

Closing a gap is safe in both directions: if the original fill leaked
through it, the faces on both sides take the same value from the map, so
the leak stays as the player saw it (spec section 6.3, gap rule).

## 4. Values and fallbacks

A face's value comes from the path-1 map: from the *region pixels* whose
centres it contains, that is pixels owned by a fill or left as cleared (not
by a line or brush drawn over them).

- **Uniform**: all region pixels outside the edge band (pixels with a
  neighbour of another value, among their eight) have the face's most
  frequent value. The face is drawn in it.
- **Mixed**: they disagree. The face falls back to 1x blocks: each point
  takes the value of the 1x pixel it lies in (for line and brush pixels,
  the most frequent value of their region neighbours).
- **Thin**: the face contains no region pixel (slivers along lines). It
  falls back to blocks as well.

## 5. Layers

Above the faces, in drawing order:

- **Strokes**: every visible line (one owning at least one pixel at the
  end) is a stroke 1 original pixel wide: all points within 0.5 of its
  centre line, with round ends. Lines that are fully overdrawn are left out.
- **Brushes**: a solid circle pattern is the ellipse inscribed in its box, a
  solid rectangle pattern the box itself (`sci/pic/geometry.hpp` gives the
  box path 1 uses).
- **Blocks**: a textured pattern is drawn as the 1x pixels it owns at the
  end, scaled N x N.

## 6. Curves

Chains of short lines that imitate curves are fitted
(`src/sci-vector/src/fitting.cpp`, `curves.cpp`).

- **Chains** run through vertices where exactly one other usable edge
  continues the chain and the direction turns by at most 50 degrees. Usable
  edges belong to visible lines up to 12 pixels long (in either axis) that
  were not extended to close a gap. Where other edges meet a chain, the
  vertex is *pinned*: the curve passes through it exactly.
- **Fits**, in order of preference, all keeping the chain's end points:
  a straight line; a circular arc (closed-form algebraic least squares with
  the centre on the chord's bisector); a cubic spline (Schneider's least
  squares with Newton reparametrisation), split at the middle where one
  piece does not fit; for closed chains that nothing pins, an axis-aligned
  ellipse. Pinned chains are fitted section by section with cubics that
  share tangents at the pins, so the curve stays smooth across them.
- **Acceptance of a fit**: the curve stays within 0.5 original pixels of the
  chain both ways, crosses no other edge, and encloses no other vertex
  between itself and the chain. The faces therefore stay the same; the
  curve replaces the chain's edges for both faces and carries the strokes of
  the chain's lines.
- **Arithmetic**: only +, -, *, / and sqrt (spec section 5.3). Curves are
  flattened into chords of at most 0.25 original pixels.

## 7. Rasterisation

`sci-render` samples a scene on a 16 x 16 grid per HD pixel (spec section
6.6). Sample i of a row of density d lies at x = (i + 0.5) / d.

- **Faces**: the scene's scan edges (every non-horizontal boundary, with the
  face on its +x side) are crossed with the sample row; a sample takes the
  face of the last crossing at or before it. Of crossings at the same x,
  the edge closest to +x comes last. Every sample therefore lies in exactly
  one face.
- **Layers** are painted over the faces in order, as spans: the capsule of
  each stroke segment, the ellipse, the box, the blocks.
- **Colour**: a pixel is the average of its 256 samples. Each pair's colour
  is the plain average of its two EGA colours' sRGB values (spec section
  6.5) at 16 bits per channel (v8 * 257); sums are integers, and the 8-bit
  result is round(v16 * 255 / 65535) by `((v16 * 255) + 32768) >> 16`.
  The table is built at compile time (`sci/render/colors.hpp`).
- **Fast paths**: face spans are filled in bulk, and a pixel row of 16 equal
  samples is added in one step. `reference_value` evaluates one point the
  simple way; a differential test compares the two.
- **Threads**: HD rows are tasks of the executor interface; each writes its
  own row, so the result does not depend on the threads.
- **HD priority map**: the priority scene sampled once per HD pixel, at its
  centre, so occlusion edges follow the vectors (spec section 6.6).
- **Original mode**: the 1x visual map, every pixel an N x N block.

## 8. Acceptance

`check_acceptance` (`sci/render/acceptance.hpp`) reduces the visual scene to
1x (per pixel the value covering most of its 16 x 16 samples) and compares
pair classes (pairs compared by the colour they blend to) with path 1's
pair map outside the edge band: the agreement must be at least 99% and no
group of four or more differing pixels may remain (a swap). Every pixel a
visible line owns at 1x must be touched by its stroke.

## 9. Determinism

Topology is exact. Fitting and rasterisation use `double` under the
floating-point profile of spec section 5.3; orders that reach the output
use stable sorts with explicit tie-breakers. Golden hashes of synthetic
scenes run in CI on x86-64 and ARM64, and those of every picture of the
tested release are checked locally (`tools/golden-hashes`), also on ARM64
under qemu-user.
