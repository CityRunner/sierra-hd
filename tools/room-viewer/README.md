# room-viewer

A development tool that renders pictures, views and fonts at 1x through
path 1, sci-view and sci-text, and writes PNG files. Until the window layer
exists (milestone 3a) it is command-line only.

    sci-room-viewer <game> pic <n>[+<n>...] [--map visual|priority|control|all]
                    [--mirrored] [--palette <0-3>] [--scale <k>] <out.png>
    sci-room-viewer <game> view|font <n> [--scale <k>] <out.png>
    sci-room-viewer <game> all <directory>

- `pic 12+45` draws picture 12, then adds picture 45 on top, as rooms do.
- `--map all` puts the visual, priority and control maps side by side.
  Every map is shown in the 16 EGA colours.
- `view` writes one row per loop with all its cels (transparent pixels show
  the background); `font` writes all glyphs, 16 per row.
- `all` writes every picture, view and font into the directory.

The output is derived from the game art. Write it only to git-ignored
places such as `build/` or `reports/images/`; never commit it.
