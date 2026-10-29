# hd-frames

Runs the game without a window, on virtual time only, and writes the frames
the program would present at chosen ticks: original mode and HD mode side
by side at N (milestone 5, `docs/hd-composition.md`). The frames are
composed by the program's own code (`sci/platform/game_frame.hpp`), and the
run is deterministic: a replay file, a seed and virtual time.

    sci-hd-frames <game folder> <output folder> --at <tick>[,<tick>...]
                  [--replay <file>] [--seed <n>] [--scale <n>] [--snap]
                  [--hints <directory>] [--cache <directory>]
                  [--timing <from>-<to>] [--stop-tick <n>]
    sci-hd-frames <game folder> <output folder> --stack <n>[+<n>...]
                  [--scale <n>] [--snap] [--hints <directory>] [--cache <directory>]
    sci-hd-frames <game folder> <output folder> --check-interface <from>-<to>
                  --stop-tick <n> [--replay <file>] [--seed <n>]
                  [--hints <directory>] [--cache <directory>]

- `--at` writes `<output>/<tick>.png` once the virtual time reaches each
  tick (60 per second): original | HD, and with `--snap` a third panel, HD
  with `priority_snap = on`. N is 5 unless `--scale` says otherwise.
- Saves made in the game during the run (kSaveGame) are kept in memory:
  every run starts with none, as `sci-vm-trace`'s do, so a replay that
  saves and restores through the game's dialogs renders the same frames.
- `--hints hints` uses the repository's hint files; `--cache <directory>`
  keeps the analysed scenes between runs (`docs/formats/analysis-cache.md`).
- `--timing <from>-<to>` composes the HD frame whenever the shown screen
  changes between the two ticks, as the program presents it, and prints
  the time taken per frame; the first frame includes rendering the HD
  images of the picture. Measure in a release build.
- `--stack 174+74` writes the picture stack (a picture with overlays drawn
  over it) as the interpreter would show it with nothing else on screen,
  through the analysis cache's variant of the stack.

- `--check-interface 1-16` (milestone 6b) checks every frame of the run
  whose interface changed (or whose picture did), in original and HD mode
  at every N of the range, and prints a summary; it exits with 1 if a
  check failed (`docs/hd-composition.md`, "The interface at every N"):
  - original mode: every pixel is the N x N block of its shown colour;
  - HD mode: every interface pixel is that block, and with the
    interface's colours changed the frame changes in those blocks and
    nowhere else;
  - the menu bar's rows belong to the interface.

  The frames are kept up to date as the program keeps them, one per mode
  and N. `check-interface.sh [--jobs <n>] [--scales <from>-<to>]
  [scenario file...]` runs it on the scenarios (each replay once, at its
  longest stop tick) with the release build; a scenario that passed is
  kept in `build/interface-check/<scenario>-<N range>.ok` and skipped by
  later runs.

The run ends at the last tick asked for (or `--stop-tick`) with a quit
event. The output is derived from the game art: write it only to
git-ignored places such as `build/` or `reports/images/`; never commit it.
