# graphics-compare

The milestone 2 check: pictures (path 1), views and fonts are bit-exact
with ScummVM, compared as palette-index buffers. It needs the game files,
so it runs locally, not in CI.

How it works:

1. The reference ScummVM, built with
   `tools/scummvm-patches/0002-dump-graphics.patch`, draws every picture in
   six variants (normal, mirrored, EGA palettes 1-3, added over the previous
   picture) and writes the visual, priority and control maps. It also
   writes every view cel and font glyph
   (`SIERRA_HD_DUMP_GRAPHICS=<dir>`, run under `xvfb-run`).
2. `sci-graphics-dump` (`tools/graphics-dump`) writes the same layout from
   sci-pic, sci-view and sci-text. It also checks that the pair map dithers
   to the visual map.
3. `compare.py` requires identical manifests and identical files. For a
   picture that differs it reports the number of differing pixels per map.

Run it inside the toolchain container, with the game folder mounted
read-only and the reference built as in `tools/resource-compare/README.md`:

    docker/run.sh cmake --preset release
    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh tools/graphics-compare/run.sh /game

The dumps go to `build/dumps/` (git-ignored, game-derived data). Other
builds compare the same way:

    build/asan/tools/graphics-dump/sci-graphics-dump /game build/dumps/asan-gfx
    qemu-aarch64 build/cross-aarch64/tools/graphics-dump/sci-graphics-dump /game build/dumps/arm64-gfx
    python3 tools/graphics-compare/compare.py build/dumps/scummvm-gfx build/dumps/arm64-gfx
