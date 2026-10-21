# Milestone 4a: the VM boot gate

Status: **approved** (2026-10-21). The music player stays in sci-vm, and
running the game in the program, with the analysis cache, is added to 4b.
The text below is the report as reviewed.

Exit criteria (spec section 12): scripts load and run, the first room is
reached, kernel-call traces match ScummVM.

## What was implemented

The decisions are in `docs/ASSUMPTIONS.md`, "Milestone 4a"; the formats in
`docs/formats/replay-and-trace.md`; the structure in `docs/ARCHITECTURE.md`
and `src/sci-vm/README.md`.

1. **The reference in deterministic mode** (patch `0003`): virtual clock,
   the music timer on that clock, a seeded random source, a fixed date,
   replayed input, a virtual mouse, and a trace of every kernel call.
   ScummVM's Event Recorder was evaluated first and not used: it records
   real-time host events in a binary format that only ScummVM can replay.
2. **The replay and trace formats**: a text replay file timed in game
   ticks (keys, typed text, mouse moves and clicks), and one trace line per
   kernel call with its arguments and result.
3. **sci-vm**, a port of ScummVM's SCI0 engine (about 17,000 lines),
   keeping its state model (registers with segment and offset, segments,
   heap):
   - segments, script loading and relocation, classes, objects and
     selectors, the bytecode interpreter, the garbage collector, the kernel
     table with its signatures and workarounds;
   - this game's 16 script patches, with ScummVM's signature engine;
   - 116 of the 132 kernel functions and subfunctions of the SCI0 table:
     lists, scripts, strings, maths, events, time, files, movement, menus,
     graphics and sound. The other 16 (the parser, the interactive menu,
     controls, files, saving, DoAvoider) are milestone 4b; calling one stops
     the interpreter with a message;
   - the graphics at 1x that the kernel functions drive: screen maps,
     ports and windows, painting, text, collisions, the animation cycle,
     cursor, menu bar, transitions. Pictures, views and fonts are drawn
     with sci-pic, sci-view and sci-text, which match ScummVM since
     milestone 2;
   - the music player (ScummVM's SciMusic and MidiParser_SCI) on a silent
     driver: timing, signals and cues, with the AdLib driver's timer and
     channel filter;
   - the virtual clock, the random source and the event queue.
4. **`tools/vm-trace`**: runs the game in the deterministic mode, writes
   the trace, and compares it with the reference's (`compare.py`).
5. **Tests**: unit tests of the replay parser, the event queue, the clock,
   the random source (against the values in the reference trace),
   rectangles, the music player on synthetic songs, and the script patcher
   on synthetic scripts; a fuzz target for the replay parser.

## Results

The game boots through room 99 (the speed test) to room 414 (the copy
protection), the first room, and plays there. Each run below compares the
whole trace, call by call, from the first kernel call to the stop:

| Run | Input | Seed | Ticks | Kernel calls | Result |
|---|---|---|---|---|---|
| boot | none | 0 | 3,600 (1 min) | 12,325 | identical |
| long | none | 0 | 18,000 (5 min) | 60,325 | identical |
| seed 7 | none | 7 | 3,600 | 12,325 | identical |
| seed 12345 | none | 12345 | 3,600 | 12,325 | identical |
| mouse | moves and clicks | 0 | 3,600 | 12,353 | identical |
| typed | "look" and Enter | 0 | 3,600 | 11,880 | identical |

- The seeds change what the game draws (its Random calls differ from seed
  0's from call 296 on), and the input runs differ from the idle run, so
  these are different paths through the game.
- The same six runs match with ASan and UBSan on (no sanitizer reports),
  and the boot and mouse runs match on ARM64 under qemu-user.
- The traces cover 46 different kernel functions. Room 414 starts a song
  (DoSoundPlay), so the music player and its timer run through every
  trace.

What the comparison found on the way, now fixed:

- the speed test of room 99 differed until ScummVM's script patches were
  ported (ScummVM patches four places in this game's scripts);
- the run's end: the reference stops at the next event poll after the
  stop tick, not at once;
- `strcmp`: the C library fixes only the sign of the result, and the game
  passes the value on. The optimised build returned 1 where glibc (the
  reference) returns 70. sci-vm now computes glibc's value itself.

## Tests and style

- All 19 test suites pass in the debug, ASan and UBSan builds, and the
  sci-vm tests on ARM64.
- clang-format, clang-tidy and `scripts/check-repo-policy.py` report no
  findings. Complexity suppressions are in place with a reason, as the spec
  allows for the bytecode dispatch and other ported functions.
- CI was not run (GitHub CI limits exceeded); all checks above ran
  locally in the toolchain container.

## Deviations from the spec

None from section 2. For your confirmation (both in `docs/ASSUMPTIONS.md`):

- **The music player is in sci-vm**, where spec section 4.1 lists the
  silent driver under sci-audio. Like ScummVM's, it keys songs by VM
  object and writes their signals, so it sits with the kernel functions.
  The audible devices go into sci-audio at milestone 7, under this player.
- **The program does not run the game yet**, and the analysis cache
  (deferred to milestone 4 at the 3a gate) is not done. Gate 4a needs only
  the deterministic runs. I propose both for 4b.

## Open questions

1. **The music player in sci-vm**: is this placement acceptable?
2. **4b scope**: the remaining kernel functions and scenario recordings
   (spec), plus running the game in the program with the analysis cache.
   Do you agree to add the latter to 4b, or should it wait for a later
   milestone?
