# Architecture

How the program is organised, how data flows through it, and the trade-offs
behind it. Updated at every milestone (spec section 5.1). State: milestone
4a, the VM boot gate.

## Modules

One CMake target per library under `src/`, each with a README stating its
purpose and invariants:

| Library | Purpose | Depends on |
|---|---|---|
| sci-core | Error type, assertions, bounds-checked reading, floating-point profile, logging, exact rationals and rounding, SHA-256, MD5 (release detection only), a strict JSON reader, the executor interface | none |
| sci-resource | RESOURCE.MAP, volumes, patch files, LZW and Huffman decompression (done, milestone 1) | sci-core |
| sci-pic | PIC parsing and the exact 1x rasteriser (path 1) (done, milestone 2) | sci-core, sci-resource |
| sci-vector | Vector analysis of PIC (path 2): planar graph, gap closing, faces, fallbacks, curve fitting (done, milestone 3), hints (done, milestone 3a), stroke integrity (milestone 3b) | sci-core, sci-pic |
| sci-view | View, loop and cel decoding (done, milestone 2); SCI0 cursors (milestone 6b) | sci-core, sci-resource |
| sci-text | Fonts (done, milestone 2), messages, vocabulary, Said | sci-core, sci-resource |
| sci-audio | Sound playback, silent driver, AdLib (the OPL core on Nuked OPL3 and the SCI0 driver, milestone 7), MT-32, mixer | sci-core, sci-resource; Nuked OPL3 and mt32emu (vendored) |
| sci-render | HD rasteriser (done, milestone 3), original mode, acceptance check, A1 checks (milestone 3b), composition of the game screen (milestone 5) | sci-core, sci-pic, sci-vector, sci-view |
| sci-vm | Bytecode interpreter, objects, kernel functions, the 1x graphics they drive, the music player with a silent driver or a sci-audio device (milestone 7), events, virtual clock, random source (boot gate, milestone 4a) | sci-core, sci-resource, sci-pic, sci-view, sci-text, sci-audio |
| sci-platform (in `src/app/platform`) | The parts of app without a window, shared with the tools: thread executor, game-folder reading, release identification, configuration file, hint loading (done, milestone 3a), analysis cache (milestone 4b), the HD images of the picture shown and the game's frame (milestone 5) | sci-core, sci-resource, sci-pic, sci-vector, sci-render, sci-vm |
| app (`sierra-hd`) | Window, input, presentation (the room viewer at release v0.1), saves, cache | all libraries |

`tests/` holds the unit tests (`tests/unit`), the fuzz targets
(`tests/fuzz`) and the golden tests (`tests/golden`: synthetic scenes in
CI, and the hashes of every picture for the local check). `tools/` holds the
development tools and the reference tooling, which is never linked into the
program:

- `reference-scummvm`: builds and runs the reference ScummVM.
- `scummvm-patches`: the reference's dump and trace patches.
- `resource-dump`: dumps all resources through sci-resource.
- `resource-compare`: the milestone 1 comparison with ScummVM.
- `common`: opening a game, logging and assertions to stderr, a PNG
  writer and picture drawing with the trace, shared by the tools (which
  also link `sci-platform`).
- `graphics-dump`: dumps every picture variant, view and font through
  sci-pic, sci-view and sci-text.
- `graphics-compare`: the milestone 2 comparison with ScummVM.
- `room-viewer`: renders pictures (1x maps, or HD frames and priority maps
  with `--hd`), views and fonts to PNG files; the window version is the
  program itself from release v0.1.
- `hd-report`: the milestone 3 replacement report and acceptance check.
- `golden-hashes`: writes or checks the golden hashes of every picture.
- `hint-tool`: proposes hint candidates for review, checks hint files
  against the game, and turns review decisions into hint files.
- `vm-trace`: runs the game in sci-vm's deterministic mode, writes the
  kernel-call trace and compares it with the reference's (milestone 4).
- `sound-render`: plays sound resources through the music player and the
  AdLib driver and prints a hash of the OPL output (milestone 7).
- `hd-frames`: runs the game without a window on virtual time and writes
  the frames the program would show at chosen ticks, original and HD side
  by side, and measures the composition time (milestone 5).

### Dependency rules and how they are enforced

- `sci_add_library` (`cmake/SciTargets.cmake`) accepts dependencies only on
  project libraries declared earlier in the top-level `CMakeLists.txt`. The
  graph is therefore acyclic by construction, and an attempt to add a
  cycle, or an unknown target, fails at configure time.
- Each library's `include/` directory is public and its `src/` directory is
  private. Another library cannot include another library's internals.
- External dependencies are not accepted by `sci_add_library` at all. The
  vendored libraries (`cmake/SciThirdParty.cmake`) are linked by name only
  where spec section 4.2 allows them: SDL3 by app (which holds the audio
  output too, `src/app/src/audio_output.cpp`), Nuked OPL3 and mt32emu privately by sci-audio, whose public
  headers do not include them.
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
4. sci-vm's music player plays the sound resources on the virtual clock
   (signals and cues go back to the scripts) and sends MIDI to a sci-audio
   device; in the program the AdLib driver's chip output is mixed to the
   device rate on the main thread and handed to SDL's audio thread through
   an `SDL_AudioStream` (milestone 7).
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
   chains of short lines and to the chains the hint files name, and lists
   strokes, brushes and textured blocks as layers in drawing order; in
   the visual plane, the stroke integrity pass then clips line ends at the
   contours they end on, closes joints and removes lines along the pixels
   where the player saw something else (spec section 6.10);
3. `sci::render::render_hd` samples the scene on a 16 x 16 grid per pixel,
   rows in parallel through the executor; `render_priority` gives the HD
   priority map; `render_original` the original mode;
4. `check_acceptance` compares the scene, reduced to 1x, with path 1, and
   `check_integrity` runs the A1 checks (tools only).

A scene is plain data (`sci/vector/scene.hpp`): faces with values, scan
edges with the face after them, and layers. sci-render needs nothing else,
so the scene can later be cached (spec section 10.2), and the hints only
change how it is built.

## The interpreter (milestones 4a and 4b)

sci-vm is a port of ScummVM's SCI0 engine, kept close to ScummVM's
structure so that each part can be checked against its source
(`src/sci-vm/README.md` maps the files):

- `Machine` holds the state ScummVM keeps in `EngineState`: the segment
  manager, the execution stack, the accumulator. `run_vm` executes
  bytecode; a `callk` goes through the kernel table, which checks the
  argument signature and applies ScummVM's workarounds before calling the
  function.
- `Subsystems` holds what ScummVM keeps in `SciEngine`: the virtual clock,
  the random source, the event queue, the graphics, the sound and the
  parser, the files the game opens (through a host `FileStore`) and the
  saves. Kernel functions reach them through the machine.
- `Graphics` ties together the ported GfxScreen, GfxPorts, GfxPaint16,
  GfxText16, GfxCompare, GfxAnimate, GfxCursor, GfxMenu and GfxTransitions.
  The screen is the 320 x 200 visual, priority and control maps that the
  game logic reads; pictures are drawn by sci-pic. Beside them the screen
  keeps, for the HD composition (`docs/hd-composition.md`), the owner map
  (what drew each visual pixel), the priority source map, the background
  layer under the sprites and the sprite lists; and of all this what the
  player sees (what bitsShow and the transitions copied), which the
  program presents. Paint keeps the picture stack shown (base and
  overlays) and the kernel lines drawn into it.
- The text parser (vocabulary, grammar, Said specs) lives in sci-text
  (`sci/text/parser.hpp`), free of VM types; kParse and kSaid connect it
  to the game's objects.
- Script patches are applied when a script loads, as in ScummVM.
- The music player runs on the virtual clock (its timer once per 4 ms of
  virtual time) and plays to a silent driver; only timing, signals and
  cues reach the scripts.

Two entry points run the game:
- `run_deterministic` (`include/sci/vm/interpreter.hpp`) runs it the way
  the patched reference does and reports each kernel call and, on request,
  the shown screen, the actors, the parser's results and the window layout
  (`Graphics::set_layout_log`: the drawing code reports windows, text,
  controls, menus and the cursor in 1x, milestone 6b); `tools/vm-trace`
  uses it.
- `Game` (`include/sci/vm/game.hpp`) runs it for the program, on the
  calling (main) thread. Wherever virtual time would pass, sci-vm calls
  `GameHost::wait`; the host presents `Game::screen()`, passes input on
  with `Game::input` and returns when that much real time has passed,
  calling `Game::pass_wait` meanwhile so that the music timer calls due
  run as real time passes (milestone 7). The host also gives the local
  time. Replays can be scheduled into a run.

## The program

`src/app/src/main.cpp` does the following:
1. reads `sierra-hd.ini`;
2. identifies the game folder (`sci-platform`);
3. opens an SDL3 window and prepares the analysis cache;
4. runs the game (`sci::vm::Game`, milestone 4b), or with `--viewer` drives
   the room viewer of release v0.1.

In the game, `GameFrame` (sci-platform) composes the 320N x 200N frame
from the shown screen with sci-render's `compose` (milestone 5,
`docs/hd-composition.md`). In HD mode the interface (text, windows, menus)
is drawn as N x N blocks on top; under it, the background layer comes from
the HD background where the picture drew it (`HdPictures`, rendered from
the cached scene of the picture stack at N) and is a block elsewhere; the
sprites are original cels scaled N x N, hidden behind the picture by the
HD priority map (rendered from the priority scene), or by the 1x priority
where something else set it or with `priority_snap = on`. Only the 1x
pixels whose shown data changed are composed again. In original mode every
pixel is an EGA block. The game's cursor, scaled N x N, becomes the
window's cursor where the system shows it pixel for pixel, and is drawn
over the frame by the presenter elsewhere (milestone 6b,
`docs/hd-composition.md`, "The cursor"); the mouse reaches the game in 1x
through one tested mapping (`game_screen.hpp`). Keys are translated as
ScummVM translates them. The game's own files are
kept in `files/` next to the configuration (`DirectoryFileStore`).

`Viewer` composes the 320N x 200N frame on the CPU: the status line, the
background (cached per picture, mode, map and N) and a view cel or a font
sheet. It records the changed rectangles, and the window uploads only
those to a streaming texture. The texture is drawn 1:1 into an integer
viewport in the middle of the window; the rest is letterbox.

## The analysis cache (milestone 4b)

The vector scenes are kept in files (spec section 10.2;
`docs/formats/analysis-cache.md`):
- `sci-vector` writes and reads a scene as bytes (`scene_io.hpp`), so it
  stays free of I/O.
- `sci-platform`'s `AnalysisCache` names, keys, reads and writes the
  entries in `cache/` next to `sierra-hd.ini`.
- On launch, `main.cpp` has the cache analyse the missing entries on the
  executor, behind a progress bar.
- `Viewer` and the game mode's `HdPictures` ask `AnalysisCache::scene`
  for a scene. A picture with overlays drawn over it (a picture stack) is
  a variant of its own (milestone 5). The scene comes from memory or from its file, or it is analysed
  then and there.

The key covers the game data, the analyser version, the hint file and the
variant, so a stale entry is never used.

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
- The interpreter's results do not depend on the C library either: string
  comparison returns glibc's values (the reference's), sin and cos come
  from a table of glibc's values, and the two kernel functions that
  ScummVM computes in binary32 round each step to binary32 in `double`
  (`docs/ASSUMPTIONS.md`, "Milestone 4a"). The kernel-call traces match on
  x86-64, under ASan and UBSan, and on ARM64 under qemu-user.
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
- **Stroke integrity as a pass over the built strokes.** The pass edits
  strokes (end points, cuts, connectors, removed runs) after the scene is
  built, so faces, fitting and acceptance stay as they were and every
  change is recorded with its evidence. The cost: it works with 1 pixel
  wide round strokes and cannot change how such strokes meet the 1x pixel
  pattern, so some A1 cases remain (`reports/milestone-3b.md`).
- **A parsed command stream instead of drawing while decoding.** ScummVM
  draws as it decodes. Parsing first costs one allocation per picture, and
  in return the parser and the rasteriser are tested separately and path 2
  shares the parser.
