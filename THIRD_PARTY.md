# Third-party components

Every third-party component with its version and licence (spec section 14).
Vendored code lives in `third_party/` at a pinned commit or checksum,
together with its licence file.

## Vendored

None at milestone 0. Planned, with the pin recorded when each is added
(docs/ASSUMPTIONS.md, "Third-party components"):

| Component | Licence | Needed from |
|---|---|---|
| SDL3 | zlib | milestone 3a (window layer) |
| Nuked OPL3 | LGPL 2.1 or later | milestone 7 |
| mt32emu (Munt) | LGPL 2.1 or later | milestone 7 |

## Ported code

Code ported from ScummVM (GPL v3 or later, pinned revision in
docs/ASSUMPTIONS.md). Each file keeps ScummVM's licence notice and names the
functions and source files it comes from.

| Project file | ScummVM source |
|---|---|
| `src/sci-resource/src/resource_id.cpp` | `engines/sci/resource/resource.cpp` (type tables, `convertResType`) |
| `src/sci-resource/src/resource_map.cpp` | `engines/sci/resource/resource.cpp` (`detectMapVersion`, `readResourceMapSCI0`) |
| `src/sci-resource/src/volume.cpp` | `engines/sci/resource/resource.cpp` (`detectVolVersion`, `readResourceInfo`) |
| `src/sci-resource/src/decompress.cpp` | `engines/sci/resource/decompressor.cpp` (`Decompressor`, `DecompressorHuffman`, `DecompressorLZW::unpackLZW`) |
| `src/sci-resource/src/patch_file.cpp` | `engines/sci/resource/resource.cpp` (`readResourcePatches`, `processPatch`) |
| `src/sci-resource/src/resource_manager.cpp` | `engines/sci/resource/resource.cpp` (sources, index and loading) |
| `src/sci-pic/src/parse_pic.cpp` | `engines/sci/graphics/picture.cpp` (`GfxPicture::draw`, `drawVectorData`, coordinate and pattern decoding) |
| `src/sci-pic/src/draw_pic.cpp` | `engines/sci/graphics/picture.cpp` (`drawVectorData`, default EGA palette and priority tables), `engines/sci/graphics/paint16.cpp` (`drawPicture`, `clearScreen`) |
| `src/sci-pic/src/canvas.cpp` | `engines/sci/graphics/screen.cpp` (`drawLine`, `dither`, `vectorIsFillMatch`), `engines/sci/graphics/picture.cpp` (`vectorFloodFill`) |
| `src/sci-pic/src/patterns.cpp` | `engines/sci/graphics/picture.cpp` (`vectorPattern*`) |
| `src/sci-pic/src/pattern_tables.hpp` | `engines/sci/graphics/picture.cpp` (`vectorPatternCircles`, `vectorPatternTextures`, `vectorPatternTextureOffset`), generated |
| `src/sci-view/src/view.cpp` | `engines/sci/graphics/view.cpp` (`GfxView::initData` and `unpackCelData` for EGA views, `getBitmap`) |
| `src/sci-text/src/font.cpp` | `engines/sci/graphics/scifont.cpp` (`GfxFontFromResource`) |

`tools/scummvm-patches/` holds patches to ScummVM itself (GPL v3 or later),
used only for the reference build.

## Tools (not distributed)

| Tool | Version | Licence | Use |
|---|---|---|---|
| LLVM (Clang, lld, clang-format, clang-tidy, libFuzzer, sanitizers) | 23.1.4 | Apache 2.0 with LLVM exception | compiler and analysis |
| ScummVM | pinned revision, see docs/ASSUMPTIONS.md | GPL v3 or later | behavioural reference, built under tools/ |
| Ubuntu 24.04 packages | release pocket | various | toolchain container |
