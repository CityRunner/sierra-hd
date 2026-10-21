# hint-tool

Tooling for the scene-analysis hints (spec section 6.7, `hints/README.md`).

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/release/tools/hint-tool/sci-hint-tool /game candidates 44 build/cand
    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/release/tools/hint-tool/sci-hint-tool /game check hints

- `candidates <n> <directory>`: proposes the chains of picture n that may
  be curves the automatic fitting missed: runs of three or more connected
  visible lines that turn (at most 75 degrees at a joint) and are not all
  fitted already. Each candidate is tried as an arc, a spline and a line
  under the hint rules. The command writes:
  - `pic.NNN.candidates.json`: per candidate, the lines, the accepted kinds
    and the points;
  - `pic.NNN.candidates.png`: the picture, dimmed, with the accepted
    candidates numbered. It is derived from the game art; keep it in
    git-ignored places (`build/`).

  It also prints the picture's PIC SHA-256 for the hint file.
- `check <directory>`: the local hint-anchor check. Every hint file must
  parse, match its picture's data, and refer only to commands that draw
  lines. The command prints what the vector path does with each file and
  exits with 1 on a problem. It needs the game files, so CI runs only the
  schema check (`scripts/check-hints.py`).
- `make_hints.py <candidates> <decisions> <hints> <analyser>`: turns the
  review decisions (one JSON file per picture; see the script's header)
  into hint files. The committed hints were made this way; see
  `docs/ASSUMPTIONS.md`, "Milestone 3a".
