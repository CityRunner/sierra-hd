# SCI0 pictures (PIC resources)

How the program decodes and draws pictures on path 1 (`src/sci-pic`), the
exact 1x rasteriser of spec section 6.2. Ported from ScummVM's `GfxPicture`
and `GfxScreen` at the pinned revision. Checked against the reference for
every picture of the tested release, in six variants (`tools/graphics-compare`).

## Screen and maps

- The screen is 320x200 and has three maps: visual (EGA colour 0-15),
  priority (0-15) and control (0-15).
- A picture is drawn through the picture port, which starts at row 10 (rows
  0-9 are the menu bar). Picture coordinates (x, y) land on screen row
  y + 10.
- Unless the picture is added to the current one (the overlay flag of
  `kDrawPic`), the picture port is first cleared: visual 15 (white),
  priority 0, control 0.
- Path 1 also keeps a pair map, which ScummVM does not have. For every
  visual pixel it records the EGA colour pair behind it: the low nibble is
  the colour shown where (x ^ y) is even, the high nibble where it is odd.
  The vector path reads it (spec sections 6.2 and 6.3).

## Command stream

A picture is a byte stream of opcodes (0xF0-0xFF), each followed by
parameters, ending with 0xFF. Coordinates come in three forms:

| Form | Bytes | Meaning |
|---|---|---|
| Absolute | 3 | prefix p, low x, low y; x += (p & 0xF0) << 4, y += (p & 0x0F) << 8 |
| Relative (short) | 1 | high nibble: x step (bit 7 = negative, 3 bits); low nibble: y step (bit 3 = negative, 3 bits) |
| Relative (medium) | 2 | y byte (bit 7 = negative, 7 bits), then x byte (bit 7 = negative with magnitude 128 - low 7 bits) |

Relative steps wrap at 16 bits, as in ScummVM. A coordinate list continues
while the next byte is below 0xF0.

| Opcode | Command(s) | Parameters |
|---|---|---|
| F0 | SetVisual | palette index |
| F1 | DisableVisual | |
| F2 | SetPriority | priority (low nibble) |
| F3 | DisablePriority | |
| F4 | Pattern, one per point | [texture] absolute, then [texture] short relative points |
| F5 | Line, one per segment | absolute start, then medium relative points |
| F6 | Line, one per segment | absolute start, then absolute points |
| F7 | Line, one per segment | absolute start, then short relative points |
| F8 | Fill, one per point | absolute points |
| F9 | SetPattern | pattern code: pen size (bits 0-2), rectangle (bit 4), textured (bit 5) |
| FA | Pattern, one per point | [texture] absolute points |
| FB | SetControl | control (low nibble) |
| FC | DisableControl | |
| FD | Pattern, one per point | [texture] absolute, then [texture] medium relative points |
| FE | extended, see below | |
| FF | end of picture | |

Pattern points carry a texture byte when the pattern code has bit 5 set.
The texture number is that byte >> 1.

Extended opcodes (after FE), for EGA pictures:

| Code | Command | Parameters |
|---|---|---|
| 0 | SetPaletteEntries | (index < 160, value) pairs while below 0xF0 |
| 1 | SetPalette | palette number (0-3), 40 values |
| 2 | (monochrome, ignored) | 41 bytes |
| 3, 5 | (monochrome, ignored) | 1 byte |
| 4, 6 | (monochrome, ignored) | none |
| 7 | EmbeddedView | absolute point, size word, cel data (not used by SCI0 pictures; drawing it is unsupported) |
| 8 | SetPriorityTable | 14 band boundaries |

The tested release uses every basic opcode and extended opcodes 1 to 6.
Every command records the byte offset it came from. Hints (spec section
6.7) refer to commands by their index in the stream.

## Drawing state

- The colour starts as palette entry 0 (black), priority and control
  disabled. A disabled map is not written.
- The EGA palette table holds four palettes of 40 entries. Each entry is a
  pair, high nibble h and low nibble l. All four palettes start as
  ScummVM's `vector_defaultEGApalette`, and pictures may change them.
- `SetVisual i` reads entry i counted from the selected palette (0-3, a
  `kDrawPic` parameter), so indices of 40 and above read the following
  palettes. An index beyond the table is an error; ScummVM reads out of
  bounds there.
- While drawing, the visual map holds ScummVM's code for the pair:
  e ^ (e << 4). A solid colour (h == l) stays 0-15. At the end of the
  picture every coded pixel is replaced by the colour it shows: l where
  (x ^ y) is even, h where it is odd. This is the EGA dither.
- Mirroring reflects every point's x as 319 - x, except for embedded views.

## Drawing operations

### Lines

Sierra's Bresenham variant:
- the end points are clipped to the screen;
- horizontal and vertical lines are drawn directly;
- otherwise both end points are set, then the line steps along the longer
  axis with an error term starting at (short - long / 2).

Flood fills depend on exactly which pixels it sets.

### Fills

ScummVM's `vectorFloodFill`, a span fill from the start point within the
picture port (rows 10-199):
- It tests one map: the first enabled of visual, priority, control. In the
  visual map it compares the visible colour.
- It aborts when filling with white (visual) or 0 (priority, control), and
  when the start point is not white (visual) or 0 (priority, control).
- It leaves out the maps that already hold the target value.
- A fill spreads only over white or 0 and leaks through any gap in an
  outline. This is the leak the vector path must keep (spec section 6.3).
- A fill starting outside the screen is an error; ScummVM reads outside its
  buffers there.

### Patterns

A brush stroke on a box of (2s + 2) x (2s + 1) pixels for pen size s,
centred on the point.
- The box is moved inside the left and top picture edges, offset into the
  port, then moved inside the bottom edge and to at most one column beyond
  the right edge.
- Rectangles fill the box. Circles use Sierra's per-size bitmaps, one bit
  per box pixel.
- Textured patterns draw only where Sierra's 255-bit noise table (stored
  twice, back to back) has a set bit. Reading starts at the texture
  number's offset and advances per box pixel for rectangles, per circle
  pixel for circles.
- A rectangle pixel in the column beyond the right edge wraps to the start
  of the next row, with the two dither colours swapped.

## Views in pictures, and priority bands

The tested release's pictures contain no embedded views and no priority
tables. The game sets priority bands through kernel calls (milestone 4).
