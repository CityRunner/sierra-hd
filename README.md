# sierra-hd

An HD version of Sierra's *The Colonel's Bequest* (DOS, EGA, SCI0), running
on its own SCI0 interpreter written in C++23. The vector backgrounds are
re-rendered at high resolution; sprites stay original and are scaled by an
integer factor. Game logic behaves exactly as in the original.

**Status: milestone 4b (kernel coverage).**
- The interpreter runs the game: every kernel function the game's scripts
  use is implemented, with the menus, dialogs and the text parser.
- Scenario recordings across the main areas give the same kernel calls and
  the same screens as ScummVM, call by call and pixel for pixel.
- In HD mode the backgrounds are vector art at any integer scale. Sprites
  and text are still drawn as enlarged original pixels; HD sprites come
  later. Sound is silent until milestone 7.
- The milestone reports are in `reports/`.

You need your own copy of the game (English DOS EGA release; the GOG
release works). No game files are included, and none are ever committed.

## Building

The supported compiler is upstream LLVM Clang 23.1.4 (clang-cl on Windows)
with CMake 3.28 or later and Ninja.

### Linux, in the pinned toolchain container (recommended)

    docker/build-image.sh                       # once; tags sierra-hd-toolchain:<hash>
    docker/run.sh cmake --workflow --preset release

The other presets are `debug`, `asan` (ASan + UBSan), `tsan`, `fuzz` and
`cross-aarch64` (ARM64 under qemu-user). Behind a TLS-intercepting proxy, set
`SIERRA_HD_EXTRA_CA=/path/to/ca-bundle.crt` for `docker/build-image.sh`.

### macOS

Install LLVM 23.1.4 from the official release archive, put its `bin`
directory first on `PATH`, set `SDKROOT=$(xcrun --show-sdk-path)`, and run
`cmake --workflow --preset release`.

### Windows

From a Visual Studio developer prompt with LLVM 23.1.4 first on `PATH`, run
`cmake --workflow --preset windows-release` (or `windows-debug`,
`windows-asan`).

## Running

    build/release/src/app/sierra-hd /path/to/game            # the game
    build/release/src/app/sierra-hd --viewer /path/to/game   # the room viewer of release 0.1

- **In the game:** F12 switches between HD and the original look, and F11
  or Alt+Enter toggles fullscreen.
- **The room viewer** shows every picture (HD or original, visual or
  priority map), every view with its animation, and every font.
- **Analysis:** the first launch analyses all pictures for HD, behind a
  progress bar, and keeps the result in `cache/`.
- **Files:** the program reads `sierra-hd.ini` and `hints/` next to the
  executable. In a build tree, copy `hints/` next to it, or the HD pictures
  are drawn without hints.
- **Saves:** the game's own File menu saves and restores; the saves go to
  `saves/` next to the configuration, one file per slot
  (`sierra-hd-001.sav` to `sierra-hd-099.sav`,
  `docs/formats/save-format.md`), and the files the game itself writes to
  `files/`.
- **Help:** `sierra-hd --help` lists the keys and options.
  `docs/USER-README.md` is the user guide that ships with the binaries.

Release binaries for Linux, Windows and macOS are built by the release
workflow (`.github/workflows/release.yml`) for version tags and attached to
the GitHub releases.

The development room viewer writes the same images to PNG files
(`tools/room-viewer/README.md`):

    build/release/tools/room-viewer/sci-room-viewer /path/to/game pic 1 --map all --scale 2 build/pic1.png
    build/release/tools/room-viewer/sci-room-viewer /path/to/game pic 1 --hd --hints hints --scale 5 build/pic1-hd.png

## Checks

    scripts/pre-push.sh                                # everything below, the sanitizers, fuzzing and ARM64
    docker/run.sh scripts/check-format.sh              # clang-format
    docker/run.sh python3 scripts/check-repo-policy.py # no game data, no float
    docker/run.sh python3 scripts/check-hints.py       # hint files against their schema
    docker/run.sh scripts/run-clang-tidy.sh build/debug

## Documentation

- `docs/ARCHITECTURE.md`: modules, data flow, error handling, trade-offs
- `docs/SPEC_ADDENDA.md`: the customer's additions to the task spec
- `docs/ASSUMPTIONS.md`: every assumption and decision, including the pinned
  ScummVM revision
- `docs/formats/`: the resource, picture, view and font formats
- `docs/hd-pipeline.md`: how pictures become HD backgrounds
- `hints/README.md`: the scene-analysis hints and their format
- `tools/reference-scummvm/README.md`: the reference ScummVM build
- `reports/`: milestone reports
- `THIRD_PARTY.md`: third-party components and their licences

## How it is developed

The project is built milestone by milestone against a written
specification, with ScummVM as the behavioural reference. The changes to
that specification are in `docs/SPEC_ADDENDA.md`, every decision is in
`docs/ASSUMPTIONS.md`, and each milestone ends with a report in `reports/`.
Most of the code is written by Claude, Anthropic's AI model, through Claude
Code. See `CONTRIBUTING.md` before opening an issue or a pull request.

## Licence and trademarks

GPL version 3 or later (see `LICENSE`). The SCI logic is ported from
ScummVM, which is GPL v3 or later; `THIRD_PARTY.md` lists the ported code
and the vendored components.

This is an unofficial project, not affiliated with or endorsed by Sierra,
Activision or ScummVM. *The Colonel's Bequest* and Sierra are trademarks of
their respective owners. The repository contains no game data: you need
your own copy of the game.
