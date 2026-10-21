# hd-report

The milestone 3 replacement report (spec sections 6.4 and 12). For every
picture of a game folder it builds the visual and priority scenes and
lists:

- strokes and PIC lines, gap closures (extensions, joins, perpendicular
  connectors), faces and fallbacks (thin and mixed, with every mixed face's
  pixels and bounding box), fitted chains (lines, arcs, splines, ellipses)
  and brushes;
- the acceptance check (`sci::render::check_acceptance`): agreement outside
  the edge band, swaps, line pixels untouched or overlapped;
- the room-entry time: path 1, both scenes, the HD frame and priority map
  at the chosen N (threads: one per hardware thread);
- with `--hints <directory>` (the repository's are in `hints/`), the hints
  per picture, and every hint used or left out, with what it changed or
  why it was left out (spec section 6.7).

It exits with 1 if any picture fails the acceptance check.

    docker/run.sh cmake --build build/release --target sci-hd-report
    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/release/tools/hd-report/sci-hd-report /game build/hd-report.md [--scale 10] [--images reports/images/hd] [--hints hints]

The report holds numbers only. `--images` writes, per picture, the original
and the HD frame side by side at scale 3; they are derived from the game
art and must stay in git-ignored places.
