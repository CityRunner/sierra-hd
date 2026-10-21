# room-viewer

A development tool that renders pictures (at 1x through path 1, or in HD
through the vector path), views and fonts, and writes PNG files. The window
version is the program itself (`sierra-hd`, release v0.1); this tool stays
for scripted and pixel-exact output.

    sci-room-viewer <game> pic <n>[+<n>...] [--map visual|priority|control|all]
                    [--mirrored] [--palette <0-3>] [--scale <k>] [--hd]
                    [--hints <directory>] <out.png>
    sci-room-viewer <game> view|font <n> [--scale <k>] <out.png>
    sci-room-viewer <game> all <directory>

- `pic 12+45` draws picture 12, then adds picture 45 on top, as rooms do.
- `--map all` puts the visual, priority and control maps side by side.
  Every map is shown in the 16 EGA colours.
- `--hd` renders the HD frame at scale k, or with `--map priority` the HD
  priority map. In builds with the CMake option `SCI_DEBUG_HIGHLIGHT`,
  `--highlight` shows fitted strokes in light magenta and fallback blocks
  in yellow. `--hints hints` applies the repository's hint files.
- `view` writes one row per loop with all its cels (transparent pixels show
  the background); `font` writes all glyphs, 16 per row.
- `all` writes every picture, view and font into the directory.

The output is derived from the game art. Write it only to git-ignored
places such as `build/` or `reports/images/`; never commit it.
