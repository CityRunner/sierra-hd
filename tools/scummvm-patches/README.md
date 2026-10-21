# ScummVM patches

Patches applied to the pinned ScummVM revision by
`tools/reference-scummvm/build.sh`, in name order. Each file is a unified
diff against that revision, named `NNNN-short-description.patch`. It starts
with a comment saying what it adds and which milestone needs it. The build
exports the tree again whenever a patch changes.

| Patch | Milestone | Adds |
|---|---|---|
| `0001-dump-resources.patch` | 1 | With `SIERRA_HD_DUMP_RESOURCES=<dir>`, dump every resource as the resource manager loads it, then exit |
| `0002-dump-graphics.patch` | 2 | With `SIERRA_HD_DUMP_GRAPHICS=<dir>`, draw every picture (normal, mirrored, EGA palettes 1-3, added over the previous picture) and dump the visual, priority and control maps; dump every view cel and font glyph; then exit |
| `0003-deterministic-trace.patch` | 4 | With `SIERRA_HD_TRACE=<file>`, the deterministic mode (virtual clock, music timer on that clock, seed `SIERRA_HD_SEED`, fixed date, input from `SIERRA_HD_REPLAY`) and a trace of every kernel call; `SIERRA_HD_STOP_TICK` and `SIERRA_HD_STOP_CALLS` end the game (`docs/formats/replay-and-trace.md`) |

A trace run, with the music timing of the AdLib driver:

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh env \
        SIERRA_HD_TRACE=build/traces/reference.txt SIERRA_HD_STOP_TICK=3600 \
        xvfb-run -a env SDL_AUDIODRIVER=dummy \
        tools/reference-scummvm/run.sh build/reference-scummvm /game --music-driver=adlib
