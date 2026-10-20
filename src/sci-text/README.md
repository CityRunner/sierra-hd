# sci-text

Fonts, text and message resources, the vocabulary and Said matching.

Milestone 2 adds the fonts: `decode_font` returns the line height and one
glyph bitmap per character, ported from ScummVM's `GfxFontFromResource`
(`docs/formats/views-and-fonts.md`). Messages, the vocabulary and the parser
follow at milestone 6a.

## Invariants

- Glyphs match ScummVM as drawn by `GfxFont::draw` (8 of 8 fonts of the
  tested release, `tools/graphics-compare`).
- Glyph metrics are the original ones in 1x coordinates.
- Parser results match ScummVM for the same typed input (milestone 6a).
