# sierra-hd

An HD version of Sierra's *The Colonel's Bequest* (DOS, EGA, SCI0), running
on its own SCI0 interpreter written in C++23. The vector backgrounds are
re-rendered at high resolution; sprites stay original and are scaled by an
integer factor. Game logic behaves exactly as in the original.

**Status: milestone 3.** Resources load; pictures, views and fonts draw at 1x
exactly as in ScummVM; and pictures render as HD vector backgrounds at any
integer scale. The interpreter itself does not exist yet.

You need your own copy of the game (English DOS EGA release). No game files
are included, and none are ever committed.

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

    build/release/src/app/sierra-hd --version

The room viewer, a development tool, draws pictures, views and fonts from
your game folder into PNG files (`tools/room-viewer/README.md`):

    build/release/tools/room-viewer/sci-room-viewer /path/to/game pic 1 --map all --scale 2 build/pic1.png
    build/release/tools/room-viewer/sci-room-viewer /path/to/game pic 1 --hd --scale 5 build/pic1-hd.png

Pointing the program at the game folder, the configuration options and
MT-32 ROM support arrive with later milestones; this section will describe
them.

## Checks

    docker/run.sh scripts/check-format.sh              # clang-format
    docker/run.sh python3 scripts/check-repo-policy.py # no game data, no float
    docker/run.sh scripts/run-clang-tidy.sh build/debug

## Documentation

- `docs/ARCHITECTURE.md`: modules, data flow, error handling, trade-offs
- `docs/ASSUMPTIONS.md`: every assumption and decision, including the pinned
  ScummVM revision
- `docs/formats/`: the resource, picture, view and font formats
- `docs/hd-pipeline.md`: how pictures become HD backgrounds
- `tools/reference-scummvm/README.md`: the reference ScummVM build
- `reports/`: milestone reports
- `THIRD_PARTY.md`: third-party components and their licences

## Licence

GPL version 3 or later (see `LICENSE`). The SCI logic is ported from
ScummVM, which is GPL v3 or later.
