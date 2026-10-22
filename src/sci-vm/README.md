# sci-vm

The SCI0 interpreter, ported from ScummVM (spec section 7): segments,
script loading, classes, objects and selectors, the bytecode interpreter
and garbage collector, the kernel functions, the graphics at 1x that they
drive (with the menus and dialog controls), the music player with a silent
driver, the event queue, files, the virtual clock and the random source.
Only the SCI0-late paths this game uses are ported (`docs/ASSUMPTIONS.md`,
"Milestone 4a" and "Milestone 4b"). Every kernel function the game's
scripts reference is ported (`sci-vm-trace --scan-kernel` lists them);
saving and restoring report failure until the save format of milestone 8.

Public interface (`include/sci/vm`):

- `interpreter.hpp`: `run_deterministic` runs the game in the deterministic
  mode of milestone 4 and reports every kernel call as a trace line
  (`docs/formats/replay-and-trace.md`), and the shown screen on request;
- `game.hpp`: `Game` runs the game for the program, with a `GameHost` that
  waits in real time, presents the screen and gives input and the time;
- `shown_screen.hpp`: what `ShownScreen` carries for the HD composition
  (`docs/hd-composition.md`): owners, the picture stack, kernel lines,
  sprite lists;
- `files.hpp`: `FileStore`, through which the game's files are read and
  written (the VM does no I/O), and `MemoryFileStore`;
- `scan.hpp`: the kernel functions the scripts reference;
- `events.hpp`, `local_time.hpp`: input events, the replay-file parser,
  the time of day;
- `reg.hpp`: the register value (segment and offset).

Inside (`src/`), by ScummVM origin:

| Files | Ported from (engines/sci) |
|---|---|
| `reg.hpp`, `segments.*`, `seg_manager.*`, `object.*`, `script.*` | `engine/vm_types`, `segment`, `seg_manager`, `object`, `script` |
| `machine.*`, `gc.cpp`, `selectors.*`, `workarounds.*`, `kernel.*` | `engine/vm`, `gc`, `selector`, `workarounds`, `kernel`, `kernel_tables.h` |
| `script_patcher.*`, `script_patches.cpp` | `engine/script_patches` (Laura Bow 1 table) |
| `k_*.cpp` | `engine/k*.cpp` (the kernel functions) |
| `gfx_*`, `graphics.*` | `graphics/screen`, `ports`, `paint16`, `text16`, `compare`, `animate`, `cursor`, `menu`, `transitions`, `cache`, `view`, `scifont` |
| `sound.*`, `music.*`, `midi_parser.*` | `sound/soundcmd`, `music`, `midiparser_sci`, `audio/midiparser` |
| `timing.*`, `events.*` | `engine/state` (wait, throttle), `sci.cpp` (sleep), `event.cpp`, `common/random` |
| `files.*`, `k_files.cpp`, `path_match.hpp` | `engine/file`, `engine/kfile`, `common/str` (matchString) |
| `parser.hpp`, `k_parse.cpp` | `parser/vocabulary` (the game side; the parser itself is in sci-text) |
| `gfx_controls.*` | `graphics/controls16` |

## Invariants

- The ScummVM state model (segment and offset registers, segments, heap) is
  kept, so segment numbers and offsets match ScummVM's in traces.
- Time passes only on the virtual clock, and random numbers come only from
  the ported random source; nothing reads the host clock. The date and
  time of day come from the virtual clock, or from the host in the program.
- A ScummVM `error()` becomes a fault: the first message is kept and the
  interpreter stops.
- Pictures, views and fonts are decoded by sci-pic, sci-view and sci-text,
  which match ScummVM bit for bit (milestone 2).
