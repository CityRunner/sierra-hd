# Milestone 2: path 1, views, fonts and the room viewer

Status: **ready for review**. All exit criteria are met.

Exit criterion (spec section 12): path 1, views and fonts at 1x, plus the
room viewer (a development tool). Every room matches ScummVM bit for bit,
compared as palette-index buffers (visual, priority, control) produced by
the reference tooling. Screenshots are used only for spot checks.

## What was done

1. **sci-pic**, ported from ScummVM's `GfxPicture` and `GfxScreen` at the
   pinned revision. Every ported file names its source.
   - `parse_pic` turns a PIC resource into a typed command stream with
     absolute coordinates and source offsets. Path 2 (milestone 3) will
     read the same stream.
   - `draw_picture` is the path-1 rasteriser. It keeps ScummVM's integer
     algorithms: Sierra's line algorithm, the flood fill with ScummVM's
     queue order, pattern boxes and circles with their texture tables, the
     encoded EGA dither colours and the final dither pass, mirroring, the
     four EGA palettes, the priority table, and drawing over the current
     picture.
   - Besides the visual, priority and control maps, it writes the **pair
     map** that sci-vector needs: the EGA colour pair behind every visual
     pixel.
2. **sci-view**: EGA view decoding (loops, cels, run-length data,
   transparency key, mirrored loops), ported from `GfxView`.
3. **sci-text**: font decoding (line height, glyph bitmaps), ported from
   `GfxFontFromResource`.
4. **Reference tooling:**
   - `tools/scummvm-patches/0002-dump-graphics.patch` makes the reference
     ScummVM draw every picture through `GfxPaint16::drawPicture`, as
     `kDrawPic` does. It dumps the three maps for six variants: normal,
     mirrored, EGA palettes 1 to 3, and added over the previous picture.
     It also dumps every view cel and font glyph.
   - `tools/graphics-dump` writes the same layout from the project's
     libraries and checks the pair map against the visual map.
   - `tools/graphics-compare` compares the two dumps.
5. **Room viewer** (`tools/room-viewer`). It draws one picture or a stack of
   pictures (any map, mirrored, any EGA palette, integer scale), a view
   sheet or a font sheet, and writes PNG files. Until the window layer
   arrives at the end of milestone 3a it is command-line only (see
   "Deviations"). `tools/common` holds the code the tools share: game
   folder loading, logging to stderr and a PNG writer.
6. **Docs:**
   - `docs/formats/pictures.md` and `docs/formats/views-and-fonts.md`
     describe the formats and the drawing rules.
   - Also updated: the library READMEs, `docs/ARCHITECTURE.md` ("Pictures,
     views and fonts"), `docs/ASSUMPTIONS.md` ("Milestone 2"),
     THIRD_PARTY.md (ported files), the tool READMEs and the root README.

## Test results

### Comparison with ScummVM (local, game files of the tested release)

| Build of `sci-graphics-dump` | Result |
|---|---|
| Linux x86-64 release | 899 of 899 dumps identical, manifests identical |
| Linux x86-64 ASan+UBSan | 899 of 899 identical, no sanitizer reports |
| Linux ARM64 (cross build under qemu-user) | 899 of 899 identical |

The 899 dumps are:

| Kind | Count | Compared |
|---|---|---|
| Pictures, normal | 88 | visual, priority and control maps, 320 x 200 each |
| Pictures, mirrored | 88 | same |
| Pictures, EGA palettes 1, 2, 3 | 3 x 88 | same |
| Pictures over the previous picture | 87 | same |
| Views | 364 | every cel: size, displacement, transparency key, pixels |
| Fonts | 8 | every glyph: size and pixels |

- These are all 88 pictures of the game, so every room background is
  covered, in more variants than the rooms use.
- For every picture, dithering the pair map gives the visual map.
- No picture uses embedded views, so the unsupported opcode never comes up.
- Spot checks with the room viewer looked right: pictures, priority and
  control maps, views and fonts. The images stay out of the repository
  (`reports/images/` is git-ignored).

### Unit tests, fuzzing and style (container; Linux CI on every push)

| Check | Result |
|---|---|
| clang-format, repository policy, clang-tidy | pass, no findings |
| Linux x86-64 debug, release, ASan+UBSan, TSan | 10/10 tests pass in each (debug locally; the others also in CI) |
| Linux ARM64 (qemu locally; native in CI) | 10/10 tests pass |
| libFuzzer, 60 s per target | `fuzz-byte-reader` 2.74 M runs, `fuzz-decompress` 0.62 M, `fuzz-game-folder` 1.11 M, `fuzz-pic` 16 k, `fuzz-view-font` 0.52 M; no crashes, hangs, leaks or out-of-memory |

- New unit tests on synthetic data: 7 parser tests and 14 rasteriser tests
  for sci-pic, 3 view tests and 2 font tests. They cover lines, fills (leaks
  and barriers), patterns, mirroring, palettes, the priority table,
  drawing over a picture, and the error cases. The tests are built with
  `tests/unit/pic/pic_builder.hpp`, which writes PIC data.
- New fuzz targets: `fuzz-pic` (parse, then draw with random options) and
  `fuzz-view-font`. Each has a seed corpus generated by
  `tests/fuzz/make_seeds.py`.
- `fuzz-pic` runs slowly because every input draws four full maps.
- The CI results are on the pull request: Linux on every push, Windows on
  the pull request, macOS only on request.

Fuzzing found one problem before the fix. A cel header claiming 65535 x
65535 pixels made the view decoder allocate 4 GB, so it ran out of memory.
ScummVM allocates such a cel too and then reads past the resource. Such
cels are now rejected before the allocation.

## Deviations from the spec

- None from section 2.
- **The room viewer has no window yet.** Spec section 12 asks for the room
  viewer at milestone 2, but the window layer (SDL3) only appears at the end
  of milestone 3a. The viewer therefore writes PNG files for now; the window
  version follows with 3a. Recorded in `docs/ASSUMPTIONS.md`.
- Where ScummVM's behaviour is undefined, the port returns an error. Such
  data does not occur in the tested release. The cases are:
  - a fill starting off screen;
  - a colour index beyond the palette table;
  - an oversized cel.

  Embedded views in pictures (unused by this game) are rejected as
  unsupported. The full list is in `docs/ASSUMPTIONS.md`, "Milestone 2".

## Open questions

None. Milestone 3 (the vector path) builds on the
command stream and the pair map from this milestone.
