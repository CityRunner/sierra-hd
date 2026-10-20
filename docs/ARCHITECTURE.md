# Architecture

How the program is organised, how data flows through it, and the trade-offs
behind it. Updated at every milestone (spec section 5.1). State: milestone 0,
the empty skeleton.

## Modules

One CMake target per library under `src/`, each with a README stating its
purpose and invariants:

| Library | Purpose | Depends on |
|---|---|---|
| sci-core | Error type, assertions, bounds-checked reading, floating-point profile; later exact arithmetic, hashing, logging | none |
| sci-resource | RESOURCE.MAP and volumes, decompression | sci-core |
| sci-pic | PIC parsing and the exact 1x rasteriser (path 1) | sci-core, sci-resource |
| sci-vector | Vector analysis of PIC (path 2) | sci-core, sci-pic |
| sci-view | View, loop and cel decoding | sci-core, sci-resource |
| sci-text | Fonts, messages, vocabulary, Said | sci-core, sci-resource |
| sci-audio | Sound playback, silent driver, AdLib, MT-32, mixer | sci-core, sci-resource |
| sci-render | HD rasteriser and compositor | sci-core, sci-pic, sci-vector, sci-view |
| sci-vm | Bytecode interpreter, objects, kernel, animation | sci-core, sci-resource, sci-text |
| app (`sierra-hd`) | Window, input, presentation, configuration, saves, cache | all libraries |

`tests/` holds the unit tests (`tests/unit`), the fuzz targets
(`tests/fuzz`) and, from milestone 2, the golden tests. `tools/` holds the
development tools and the reference tooling, which is never linked into the
program.

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

## Determinism

- The floating-point profile of spec section 5.3 is set for every target in
  `cmake/SciCompilerFlags.cmake`. `sci::core::check_fp_profile()` verifies it
  at run time. Rendering and analysis threads will call it when they start.
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
  buildable and linked into the program at milestone 0. They go away as the
  libraries get content.
