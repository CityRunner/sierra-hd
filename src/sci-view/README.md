# sci-view

VIEW resource decoding: loops, cels and their transparency and mirroring;
SCI0 cursor resources.
Ported from ScummVM's `GfxView` (EGA views); the format is in
`docs/formats/views-and-fonts.md`.

`decode_view` returns a `View` with its loops and cels. Mirrored loops are
decoded mirrored: rows reversed and the x displacement negated, as
`GfxView::getBitmap` returns them.

`decode_cursor` returns an SCI0 cursor (milestone 6b): 16 x 16 pixels of
black, white or transparent and the hotspot, as `GfxCursor::kernelSetShape`
decodes them. The interpreter reports it in the layout file; the program
shows it scaled N x N.

## Invariants

- Decoded cels match ScummVM at the pinned revision (364 of 364 views of the
  tested release, `tools/graphics-compare`).
- Bounds-checked parsing; a cel larger than its data could describe is
  rejected before allocating (fuzz target `fuzz-view-font`).
