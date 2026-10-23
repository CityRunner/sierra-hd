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
| `0003-deterministic-trace.patch` | 4, 5, 6a, 6b | With `SIERRA_HD_TRACE=<file>`, the deterministic mode (virtual clock, music timer on that clock, seed `SIERRA_HD_SEED`, date and time from 1990-01-01 12:00:00 on the virtual clock, input from `SIERRA_HD_REPLAY`) and a trace of every kernel call; `SIERRA_HD_STOP_TICK` and `SIERRA_HD_STOP_CALLS` end the game; `SIERRA_HD_SCREENS=<dir>` writes the shown screen every `SIERRA_HD_SCREEN_EVERY` ticks (`docs/formats/replay-and-trace.md`); milestone 5: the 1x priority and control maps at the same ticks, and with `SIERRA_HD_ACTORS=<file>` the cast of every kAnimate call; milestone 6a: with `SIERRA_HD_PARSER=<file>` the input, words, parse tree or unknown word and event flags of every kParse call and the spec and match of every kSaid call; milestone 6b: with `SIERRA_HD_LAYOUT=<file>` the window layout in 1x (windows, text, controls, menus, saved boxes, the cursor) |

A trace run, with the music timing of the AdLib driver:

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh env \
        SIERRA_HD_TRACE=build/traces/reference.txt SIERRA_HD_STOP_TICK=3600 \
        xvfb-run -a env SDL_AUDIODRIVER=dummy \
        tools/reference-scummvm/run.sh build/reference-scummvm /game --music-driver=adlib

Patch 0003 is regenerated with `diff -ruN` per file it lists, against a
tree with 0001 and 0002 applied, keeping its comment header. Since
milestone 5 it also changes `engines/sci/graphics/animate.cpp`, since
milestone 6a `engines/sci/engine/kparse.cpp`, since milestone 6b
`engines/sci/engine/kgraphics.cpp` and `engines/sci/graphics/menu.cpp`,
`paint16.cpp`, `ports.cpp` and `text16.cpp`.

The build applies the patches with `git apply` inside its build folder. In
a git worktree of this repository git needs the main repository's `.git`
folder there, so mount it into the container at the same path
(`SIERRA_HD_MOUNTS=...,/path/to/repo/.git:/path/to/repo/.git`).
