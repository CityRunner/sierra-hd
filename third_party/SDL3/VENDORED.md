# SDL3, vendored

- Upstream: https://github.com/libsdl-org/SDL
- Version: 3.4.18, tag `release-3.4.18`, commit
  `829a65d769d935c4852f8159e964312c0957260a` (2026-10-02)
- Licence: zlib (`LICENSE.txt`)

Copied unchanged: `CMakeLists.txt`, `cmake/`, `include/`, `src/`,
`wayland-protocols/`, `LICENSE.txt`, `README.md`, `CREDITS.md`. Left out,
as the build does not use them: `test/`, `examples/`, `docs/`,
`build-scripts/`, `android-project/`, `VisualC/`, `VisualC-GDK/`, `Xcode/`
and the other top-level files.

To update, copy the same paths from the new release tag and record the new
version here, in `THIRD_PARTY.md` and in `docs/ASSUMPTIONS.md`.
