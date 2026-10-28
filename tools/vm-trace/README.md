# vm-trace

Runs the game in sci-vm's deterministic mode and writes the kernel-call
trace, in the format of the reference ScummVM's trace patch
(`docs/formats/replay-and-trace.md`), then compares the two.

    sci-vm-trace <game folder> <trace file> [--replay <file>] [--seed <n>]
                 [--stop-tick <n>] [--stop-calls <n>] [--callers] [--vm-log]
                 [--screens <folder>] [--screen-every <ticks>] [--actors <file>]
                 [--parser <file>] [--layout <file>] [--opl <file>]
    sci-vm-trace --scan-kernel <game folder> <output file>
    sci-vm-trace --vocabulary <game folder> <output file>

Always give a stop tick: a run without one goes on for as long as the game
does.

- `--screens` writes the screen the player sees as `<tick>.ppm`, and the
  1x priority and control maps the logic reads as `<tick>.priority.pgm`
  and `<tick>.control.pgm`, once the tick reaches each multiple of
  `--screen-every` (default 60).
- `--actors` writes the cast of every kAnimate call: one line per entry in
  drawing order with tick, call number, object, position, priority, view,
  loop, cel and signal (`docs/formats/replay-and-trace.md`).
- `--parser` writes what the parser made of every kParse call (the input,
  its words after synonyms and pronouns, the parse tree or the unknown
  word, the event's flags) and every kSaid call (the spec and how it
  matched), keyed by tick and call number
  (`docs/formats/replay-and-trace.md`).
- `--layout` writes the window layout in 1x screen coordinates: every
  window (its port, frame, saved area, style and title), text line, text
  box and measured size, control, inverted rectangle, menu bar and open
  menu, saved box and cursor change (shape and hotspot, shown or hidden,
  position set by the game), keyed by tick and kernel call
  (`docs/formats/replay-and-trace.md`, "Layout files").
- `--opl` plays the music on sci-audio's AdLib driver (with the game's
  patches; without it the player runs on the silent driver, with the same
  timing) and writes every OPL register write of the driver as
  `<timer call> <register> <value>` (`docs/formats/replay-and-trace.md`,
  "OPL files").
- `--vocabulary` lists the parser's vocabulary as sci-text reads it:
  `word <spelling> <class> <group>` for every word of vocab.000,
  `suffix <typed> <stored> <class mask> <class>` for vocab.901 and
  `synonym <script> <group> <replacement>` for the synonyms each script
  sets with kSetSynonyms (numbers in hex).
- `--scan-kernel` lists the kernel functions the scripts reference: number,
  name, the calls found and the scripts that make them.
- `--callers` appends ` @<script>:<offset>` (the instruction after the
  call) to each line, to find where a difference comes from; the reference
  does not write it, so strip it before comparing.
- `--vm-log` prints the interpreter's warnings and debug lines, among them
  the script patches applied.

The reference side, with the same options as environment variables:

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh env \
        SIERRA_HD_TRACE=build/traces/ref.txt SIERRA_HD_STOP_TICK=3600 \
        [SIERRA_HD_SEED=<n>] [SIERRA_HD_REPLAY=<file>] \
        [SIERRA_HD_SCREENS=<folder>] [SIERRA_HD_ACTORS=<file>] [SIERRA_HD_PARSER=<file>] \
        [SIERRA_HD_LAYOUT=<file>] [SIERRA_HD_OPL=<file>] \
        xvfb-run -a env SDL_AUDIODRIVER=dummy \
        tools/reference-scummvm/run.sh build/reference-scummvm /game --music-driver=adlib

Then:

    tools/vm-trace/compare.py build/traces/ref.txt build/traces/vm.txt

prints the first differing call with the calls before it, or
`traces match: <n> calls`. `--prefix` accepts a trace of ours that stops
early. `compare-screens.py <reference folder> <sci-vm folder>` compares the
screens and the maps (the reference writes them with
`SIERRA_HD_SCREENS=<folder>`) and prints `screens match: <n>` and
`maps match: <n>` (ticks), or the first difference of each.
`compare-actors.py <reference file> <sci-vm file>` compares the actor
positions and prints the first difference (tick, call, object and field)
or `actors match: <frames> frames`. `compare-parser.py <reference file>
<sci-vm file>` compares the parser results and prints the first difference
(tick, call, input or spec, and the fields that differ) or `parser match:
<n> parses, <m> said calls (...)`. `compare-layout.py <reference file>
<sci-vm file>` compares the layout files and prints the first difference
(tick, call, kind and the fields that differ; `#n` is the n-th word after
the kind) or `layout match: <n> lines (...)` with the lines of each kind.
`compare-opl.py <reference file> <sci-vm file>` compares the OPL register
writes and prints the first difference (line, timer call and the fields
that differ) or `opl match: <n> writes (timer calls <first> to <last>, <k>
key-ons)`.

`run-scenario.sh <replay> <stop tick> <output folder> [seed]` runs a replay
in both engines and compares traces, screens, maps, actor positions,
parser results, layout and OPL register writes (`SIERRA_HD_COMPARE_OPL=0`
skips the last); `run-all-scenarios.sh [--jobs <n>]` does so
for every run listed in `tests/scenarios/scenarios.txt`, one line per run,
n runs at a time (four on four cores take about 10 minutes for all).

`make-parser-scenarios.py <vocabulary file> tests/scenarios [--words <file>]`
writes the parser scenarios of milestone 6a (`tests/scenarios/parser-forms*.txt`
and `parser-words-*.txt`) from the output of `sci-vm-trace --vocabulary` and
prints their lines for `scenarios.txt` (file, stop tick, seed); `--words`
lists the scenario and command that first types each word:

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/debug/tools/vm-trace/sci-vm-trace --vocabulary /game build/vocabulary.txt
    tools/vm-trace/make-parser-scenarios.py build/vocabulary.txt tests/scenarios
