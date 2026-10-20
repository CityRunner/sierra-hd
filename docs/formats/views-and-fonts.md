# SCI0 views and fonts

How the program decodes view (`src/sci-view`) and font (`src/sci-text`)
resources. Ported from ScummVM's `GfxView` and `GfxFontFromResource` at the
pinned revision. All 364 views and 8 fonts of the tested release decode
identically to the reference (`tools/graphics-compare`).

## Views

All numbers are little-endian words unless noted.

| Offset | Field |
|---|---|
| 0 | Loop count (byte) |
| 1 | Flags (byte). 0x80 marks an SCI1 view, which is unsupported. |
| 2 | Mirror mask: bit n set means loop n is mirrored |
| 4 | Version. 1 marks an SCI1.1 view, which is unsupported. |
| 6 | Palette offset (not used by SCI0 EGA views) |
| 8 | One offset per loop |

A loop starts with its cel count, an unused word, then one offset per cel.
A cel starts with:
- width and height (words);
- displacement x (signed byte) and y (byte);
- the transparent colour (byte);
- run-length data: each byte is a run, with the length in the high nibble
  and the colour in the low nibble.

The bitmap starts as colour 0 and is filled run by run until width x height
pixels are set. In mirrored loops, every row is reversed and the
displacement x is negated.

A cel claiming more pixels than its remaining data can describe (15 per
byte) is rejected before anything is allocated. ScummVM would allocate it
and then read past the resource.

## Fonts

| Offset | Field |
|---|---|
| 0 | unused |
| 2 | Character count |
| 4 | Line height (the low byte is used) |
| 6 | One offset per character |

A glyph starts with its width and height (bytes), followed by its bitmap:
rows of (width + 7) / 8 bytes, most significant bit first.
- A character whose header lies outside the resource is an empty glyph, as
  in ScummVM.
- A bitmap running past the resource is an error.
