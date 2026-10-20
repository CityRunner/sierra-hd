# Architecture

How the program is organised, how data flows through it, and the trade-offs
behind it. Updated at every milestone (spec section 5.1). State: milestone 3,
the HD picture pipeline.

## Modules

One CMake target per library under `src/`, each with a README stating its
purpose and invariants:

| Library | Purpose | Depends on |
|---|---|---|
| sci-core | Error type, assertions, bounds-checked reading, floating-point profile, logging, exact rationals and rounding, SHA-256, the executor interface | none |
| sci-resource | RESOURCE.MAP, volumes, patch files, LZW and Huffman decompression (done, milestone 1) | sci-core |
| sci-pic | PIC parsing and the exact 1x rasteriser (path 1) (done, milestone 2) | sci-core, sci-resource |
| sci-vector | Vector analysis of PIC (path 2): planar graph, gap closing, faces, fallbacks, curve fitting (done, milestone 3; hints at 3a) | sci-core, sci-pic |
| sci-view | View, loop and cel decoding (done, milestone 2) | sci-core, sci-resource |
| sci-text | Fonts (done, milestone 2), messages, vocabulary, Said | sci-core, sci-resource |
| sci-audio | Sound playback, silent driver, AdLib, MT-32, mixer | sci-core, sci-resource |
| sci-render | HD rasteriser (done, milestone 3), original mode, acceptance check; compositor later | sci-core, sci-pic, sci-vector, sci-view |
| sci-vm | Bytecode interpreter, objects, kernel, animation | sci-core, sci-resource, sci-text |
| app (`sierra-hd`) | Window, input, presentation, configuration, saves, cache | all libraries |

`tests/` holds the unit tests (`tests/unit`), the fuzz targets
(`tests/fuzz`) and the golden tests (`tests/golden`: synthetic scenes in
CI, and the hashes of every picture for the local check). `tools/` holds the
development tools and the reference tooling, which is never linked into the
program:

- `reference-scummvm`: builds and runs the reference ScummVM.
- `scummvm-patches`: the reference's dump and trace patches.
- `resource-dump`: dumps all resources through sci-resource.
- `resource-compare`: the milestone 1 comparison with ScummVM.
- `common`: game-folder loading, logging and assertions to stderr, a PNG
  writer, picture drawing with the trace, and a thread executor, shared by
  the tools.
- `graphics-dump`: dumps every picture variant, view and font through
  sci-pic, sci-view and sci-text.
- `graphics-compare`: the milestone 2 comparison with ScummVM.
- `room-viewer`: renders pictures (1x maps, or HD frames and priority maps
  with `--hd`), views and fonts to PNG files (a window follows at milestone
  3a).
- `hd-report`: the milestone 3 replacement report and acceptance check.
- `golden-hashes`: writes or checks the golden hashes of every picture.

### Dependency rules and how they are enforced

- `sci_add_library` (`cmake/SciTargets.cmake`) accepts dependencies only on
  project libraries declared earlier in the top-level `CMakeLists.txt`. The
  graph is therefore acyclic by construction, and an attempt to add a
  cycle, or an unknown target, fails at configure time.
- Each library's `include/` directory is public and its `src/` directory is
  private. Another library cannot include another library's internals.
- External dependencies are not accepted by `sci_add_library` at all. When
  SDL3 arrives (milestone 3a), a separate helper will admit it for app and
  the audio output only (spec section 4.2).
- The dependencies listed above are the planned edges that are already
  needed. Edges such as sci-vm to sci-audio (sound kernel calls) are added
  when the code needs them, and the declaration order keeps the graph
  acyclic.

## Data flow (planned)

1. app identifies the game folder by resource hashes and hands the file
   bytes to sci-resource.
2. sci-vm runs the scripts. Kernel calls draw pictures through sci-pic (path
   1, whose visual, priority and control maps the game logic reads), and
   views through sci-view.
3. For HD mode, sci-vector turns the PIC command stream, the path-1 pair map
   and the hint files into a vector scene. sci-render rasterises it at
   320N x 190N, once per room, and composites sprites, text and windows per
   frame.
4. sci-audio plays sound resources on the audio thread; signals and cues go
   back to the VM.
5. app presents the frame with dirty rectangles and feeds input to the VM in
   1x coordinates.

## Error handling

There are no exceptions in the project: every target is built with
`-fno-exceptions`, or `/EHs-c-` and `_HAS_EXCEPTIONS=0` with clang-cl.

- **Failures caused by input or the environment** are values:
  `sci::core::Result<T>` is `std::expected<T, sci::core::Error>`, and
  `Error` carries an `ErrorCode` and a message. Parsers and loaders return
  it; callers propagate it with the monadic operations (`and_then`,
  `transform`) or by checking `has_value()`. The program reports such
  errors to the user and does not crash on bad game files.
- **Bugs** (broken invariants) are `SCI_ASSERT(condition)`. It stays enabled
  in release builds. On failure it calls the handler that app installs,
  which logs the failure (sci-core does no I/O), and then aborts.
- **Allocation failure** aborts, as is usual without exceptions.
- Ported ScummVM code keeps its behaviour. Its `error()` calls become either
  an `Error` return (bad data) or an `SCI_ASSERT` (internal inconsistency).
  The choice is made per call when porting.

`ByteReader` (`sci/core/byte_reader.hpp`) shows the pattern: every read
returns a `Result`, a failed read does not move the reader, and the fuzz
target `fuzz-byte-reader` checks both properties.

## Resources

`sci::resource::ResourceManager::open` takes the game folder's files as
bytes. It indexes them in ScummVM's order: format checks on the map and the
first volume, map entries (the first entry for an id wins), then patch
files, which replace or add resources. `load(id)` reads one resource and
decompresses it. The manager keeps no cache; caching belongs to the VM
(milestone 4), which knows when resources are no longer needed. The file
formats are in `docs/formats/resources.md`.

The decompressors keep ScummVM's way of reading: whole bytes are fetched
ahead of the bits, past the packed data and, as zeros, past the end of the
volume. This is what makes every decode match the reference, including the
end condition. Where ScummVM's behaviour is undefined (buffer overruns,
uninitialised memory, an endless loop), the port reports an error instead.
The full list is in docs/ASSUMPTIONS.md, "Milestone 1".

## Pictures, views and fonts (path 1)

sci-pic works in two steps. `parse_pic` turns a PIC resource into a vector
of `PicCommand`s: one typed operation per drawing step, with absolute
coordinates and the byte offset it came from. `draw_picture` rasterises the
commands into `PictureMaps` (visual, priority, control and pair maps, 320 x
200 bytes each), exactly as ScummVM's `GfxPicture` does. The split lets
sci-vector (milestone 3) read the same command stream that path 1 draws,
without parsing PIC data a second time.

The rasteriser keeps ScummVM's integer algorithms: Sierra's line algorithm,
the queue-based flood fill with its push order, the pattern boxes and
circles with their texture tables, the encoded dither colours and the final
dither pass. The pair map is path 1's addition for HD rendering; it records
the EGA pair behind each visual pixel. The formats are in
`docs/formats/pictures.md` and `docs/formats/views-and-fonts.md`.

sci-view decodes views into loops and cels (with mirroring already applied)
and sci-text decodes fonts into glyph bitmaps. Neither draws: compositing
belongs to sci-render, and drawing at 1x for comparison is
done by the tools.

All three libraries are pure functions of their input bytes and have
libFuzzer targets (`fuzz-pic`, `fuzz-view-font`).

## The HD picture pipeline (path 2)

`docs/hd-pipeline.md` describes it in full. In short:

1. path 1 draws the picture and records a `DrawTrace`;
2. `sci::vector::build_scene` builds, per plane, an exact planar graph of
   the lines (int64 rationals in doubled pixel coordinates), closes the gaps
   that path 1's pixels close, colours every face from the path-1 map
   (falling back to 1x blocks where the pixels disagree), fits curves to
   chains of short lines, and lists strokes, brushes and textured blocks as
   layers in drawing order;
3. `sci::render::render_hd` samples the scene on a 16 x 16 grid per pixel,
   rows in parallel through the executor; `render_priority` gives the HD
   priority map; `render_original` the original mode;
4. `check_acceptance` compares the scene, reduced to 1x, with path 1.

A scene is plain data (`sci/vector/scene.hpp`): faces with values, scan
edges with the face after them, and layers. sci-render needs nothing else,
so the scene can later be cached (spec section 10.2) and the hints of
milestone 3a only change how it is built.

## Logging

`sci/core/log.hpp` is the only logging facility. Messages have a level and a
category and go to a sink installed by app or a tool; sci-core itself does no
I/O. `General` is always on. Other categories (`Resources` so far; kernel
calls and sound signals later) are off by default and cost one atomic load
when off, so they stay in release builds.

## Determinism

- The floating-point profile of spec section 5.3 is set for every target in
  `cmake/SciCompilerFlags.cmake`. `sci::core::check_fp_profile()` verifies it
  at run time; the tools' executor calls it in every worker thread, and
  app's executor will (milestone 3a).
- Golden hashes of synthetic scenes run in CI on x86-64 and ARM64; those of
  every picture are checked locally, on ARM64 under qemu-user too.
- Functions whose results differ between platforms or standard libraries are
  banned by clang-tidy (`.clang-tidy`, `bugprone-unsafe-functions`).
  `float` and `long double` are banned by `scripts/check-repo-policy.py`.
- CI builds with three standard library implementations: libstdc++ on
  Linux, libc++ on macOS, and the MSVC STL on Windows. Differences between
  them therefore show up early.

## Trade-offs

- **In-house test harness instead of a framework.** It avoids a dependency;
  the cost is fewer features (no per-test CTest entries, no fixtures). One
  CTest test per test executable is enough at this size.
- **`#pragma once` instead of include guards.** Every supported compiler
  implements it, and it reads better; `portability-avoid-pragma-once` is
  disabled.
- **No `.at()`.** Unchecked `operator[]` is allowed, because `.at()` throws.
  Index checks come from the standard library hardening in Debug and
  sanitizer builds, and input-driven indices go through bounds-checked
  readers.
- **Placeholder `component_name()` functions** keep the empty libraries
  buildable and linked into the program. They go away as the libraries get
  content; sci-resource's went at milestone 1.
- **Whole files in memory.** The resource manager keeps the game folder's
  files in memory (about 2.6 MB for this game) instead of reading on demand.
  This keeps sci-resource free of I/O and makes loading a pure function of
  the bytes.
- **Faces coloured from the map instead of replayed fills.** Taking each
  face's value from path 1's pixels makes the vector path follow path 1 by
  construction (leaks included) and leaves only the geometry to analyse;
  the cost is a fallback where a face's pixels disagree (5 faces in the
  whole game).
- **Supersampling with fast paths, and a reference.** The rasteriser fills
  spans and adds uniform sample rows in one step; `reference_value`
  evaluates single points the simple way, and a differential test keeps
  the two equal (spec section 5.1).
- **A parsed command stream instead of drawing while decoding.** ScummVM
  draws as it decodes. Parsing first costs one allocation per picture, and
  in return the parser and the rasteriser are tested separately and path 2
  shares the parser.
