# sci-pic

PIC resource parsing into a command stream, and the exact 1x rasteriser
(path 1, spec section 6.2) producing the visual, priority, control and pair
maps. Ported from ScummVM's `GfxPicture` and `GfxScreen`; the format is in
`docs/formats/pictures.md`.

- `parse_pic.hpp`: `parse_pic` turns a resource into `PicCommand`s with
  absolute coordinates and source offsets.
- `draw_pic.hpp`: `draw_picture` draws commands into `PictureMaps`, with
  options for mirroring, adding to the current maps and the EGA palette.
- `picture_maps.hpp`: the four 320 x 200 maps.

## Invariants

- At N=1 with enhancements off, the visual, priority and control maps are
  bit-identical to ScummVM, compared as palette-index buffers
  (`tools/graphics-compare`; 527 of 527 picture variants of the tested
  release).
- Dithering the pair map gives the visual map, for every pixel the picture
  drew.
- Game logic always reads the maps of path 1.
- Integer arithmetic only. Bad data is an error, never undefined behaviour
  (fuzz target `fuzz-pic`).
