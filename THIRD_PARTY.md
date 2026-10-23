# Third-party components

Every third-party component with its version and licence (spec section 14).
Vendored code lives in `third_party/` at a pinned commit or checksum,
together with its licence file.

## Vendored

| Component | Version | Licence | Directory | Used by |
|---|---|---|---|---|
| SDL3 | 3.4.18 (`release-3.4.18`, commit `829a65d769d935c4852f8159e964312c0957260a`) | zlib | `third_party/SDL3` | app: window, input, presentation |
| Nuked OPL3 | 1.8 (commit `765ec962e473aeb767e4cba74ffdc8f588ffbfe8`, 2026-08-24; no release tags upstream) | LGPL 2.1 or later | `third_party/nuked-opl3` | sci-audio: the OPL core of the AdLib device |

The SHA-256 of each vendored file is in the component's `VENDORED.md`.

Licence compatibility: the program is GPL v3 or later. LGPL 2.1 section 3
lets LGPL code be used under the GPL (v2 or, by its "or later" clause, any
later version), and LGPL 2.1 or later also permits LGPL 3, which is GPL v3
plus permissions; Nuked OPL3 linked statically into the GPL v3 program is
therefore covered by the GPL v3 as a whole, with its LGPL notices kept and
its licence text shipped in the release archives (`licenses/`). The full
source of the program, Nuked OPL3 included, is in this repository, which
meets LGPL 2.1 section 6 as well.

Planned, with the pin recorded when each is added (docs/ASSUMPTIONS.md,
"Third-party components"):

| Component | Licence | Needed from |
|---|---|---|
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
| `src/sci-pic/src/canvas.cpp` | `engines/sci/graphics/screen.cpp` (`dither`, `vectorIsFillMatch`), `engines/sci/graphics/picture.cpp` (`vectorFloodFill`) |
| `src/sci-pic/include/sci/pic/geometry.hpp` | `engines/sci/graphics/screen.cpp` (`drawLine`), `engines/sci/graphics/picture.cpp` (pattern box of `vectorPattern`) |
| `src/sci-pic/src/patterns.cpp` | `engines/sci/graphics/picture.cpp` (`vectorPattern*`) |
| `src/sci-pic/src/pattern_tables.hpp` | `engines/sci/graphics/picture.cpp` (`vectorPatternCircles`, `vectorPatternTextures`, `vectorPatternTextureOffset`), generated |
| `src/sci-view/src/view.cpp` | `engines/sci/graphics/view.cpp` (`GfxView::initData` and `unpackCelData` for EGA views, `getBitmap`) |
| `src/sci-text/src/font.cpp` | `engines/sci/graphics/scifont.cpp` (`GfxFontFromResource`) |
| `src/sci-text/src/vocabulary.cpp` | `engines/sci/parser/vocabulary.cpp` (SCI0 vocabulary loading, `lookupWord`, tokenizing, synonyms) |
| `src/sci-text/src/grammar.cpp` | `engines/sci/parser/grammar.cpp` (`buildGNF`, `parseGNF`), `engines/sci/parser/vocabulary.cpp` (`scanForMajor`) |
| `src/sci-text/src/said.cpp` | `engines/sci/parser/said.cpp` (the Said spec parser and tree matching) |
| `src/sci-text/src/parser_internal.hpp` | `engines/sci/parser/vocabulary.h`, `grammar.cpp` (internal declarations) |
| `src/sci-vm/src/*` (68 files) | the SCI0 engine: `engines/sci/engine/*` (VM, segments, scripts, kernel functions, script patches, workarounds, savegame-free file I/O), `engines/sci/graphics/*` (screen, ports, paint16, text16, compare, animate, cursor, menu, controls16, transitions), `engines/sci/sound/*` (soundcmd, music, MIDI parser), `engines/sci/event.cpp`, `engines/sci/sci.cpp`; the mapping per file is in `src/sci-vm/README.md`, and each file names its sources |
| `src/app/src/game_screen.cpp` | `engines/sci/event.cpp` (key translation), `engines/sci/graphics/cursor.cpp` (SCI0 cursor decoding) |

`tools/scummvm-patches/` holds patches to ScummVM itself (GPL v3 or later),
used only for the reference build.

## Tools (not distributed)

| Tool | Version | Licence | Use |
|---|---|---|---|
| LLVM (Clang, lld, clang-format, clang-tidy, libFuzzer, sanitizers) | 23.1.4 | Apache 2.0 with LLVM exception | compiler and analysis |
| ScummVM | pinned revision, see docs/ASSUMPTIONS.md | GPL v3 or later | behavioural reference, built under tools/ |
| Ubuntu 24.04 packages | release pocket | various | toolchain container |
