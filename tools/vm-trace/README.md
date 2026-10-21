# vm-trace

Runs the game in sci-vm's deterministic mode and writes the kernel-call
trace, in the format of the reference ScummVM's trace patch
(`docs/formats/replay-and-trace.md`), then compares the two.

    sci-vm-trace <game folder> <trace file> [--replay <file>] [--seed <n>]
                 [--stop-tick <n>] [--stop-calls <n>] [--callers] [--vm-log]
                 [--screens <folder>] [--screen-every <ticks>]
    sci-vm-trace --scan-kernel <game folder> <output file>

Always give a stop tick: a run without one goes on for as long as the game
does.

- `--screens` writes the screen the player sees as `<tick>.ppm` once the
  tick reaches each multiple of `--screen-every` (default 60).
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
        xvfb-run -a env SDL_AUDIODRIVER=dummy \
        tools/reference-scummvm/run.sh build/reference-scummvm /game --music-driver=adlib

Then:

    tools/vm-trace/compare.py build/traces/ref.txt build/traces/vm.txt

prints the first differing call with the calls before it, or
`traces match: <n> calls`. `--prefix` accepts a trace of ours that stops
early. `compare-screens.py <reference folder> <sci-vm folder>` compares the
screens (the reference writes them with `SIERRA_HD_SCREENS=<folder>`).

`run-scenario.sh <replay> <stop tick> <output folder> [seed]` runs a replay
in both engines and compares traces and screens; `run-all-scenarios.sh`
does so for every run listed in `tests/scenarios/scenarios.txt`.
