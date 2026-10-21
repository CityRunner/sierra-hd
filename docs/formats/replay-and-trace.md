# Replayed input and kernel-call traces

The VM gates of milestone 4 compare this interpreter (`src/sci-vm`) with the
reference ScummVM call by call (spec section 7). Both run in the same
deterministic mode, take their input from the same replay file and write
their kernel calls in the same trace format. `tools/vm-trace` runs our side
and compares the traces; the reference side is the patch
`tools/scummvm-patches/0003-deterministic-trace.patch`.

## The deterministic mode

- **Virtual clock.** Time advances only where the interpreter sleeps or
  waits:
  - `kWait`, and the speed throttle of `kGameIsRestarting`, sleep until the
    requested time;
  - `kGetEvent` adds 10 ms when a script polls it in a tight loop;
  - every other kernel call takes no time.
  A game tick is 1/60 s of virtual time, as `kGetTime` counts it.
- **Music timer.** The music player runs once per base tempo of the sound
  driver (4 ms for AdLib), on the virtual clock, in the interpreter's
  thread. Its timing, signals and cues therefore depend only on the
  virtual clock.
- **Random numbers.** The random source of ScummVM (`Common::RandomSource`)
  is seeded with the seed of the run (default 0) when the game starts or
  restarts.
- **Date and time.** `kGetTime` returns Monday 1990-01-01, 12:00:00.
- **Stopping.** At the stop tick or call count the reference calls
  `quitGame()`, so the game gets a quit event at its next poll of the
  event queue and stops at that kernel call. sci-vm does the same (and,
  should the game never poll, stops a minute of virtual time later).
- **Input.** Only replayed events reach the game; host input is ignored
  except for quitting. The mouse position is virtual: it starts at the
  centre of the screen (160, 100), follows replayed mouse events and is
  moved by `kSetCursor`.

## Replay files

Plain text (UTF-8, ASCII in practice), one event per line. The first line
that is neither empty nor a comment (`#`) must be `sierra-hd replay 1`.

    sierra-hd replay 1
    # tick  kind  data
    120 key 13
    300 text look at the desk
    410 move 150 80
    412 down 150 80
    418 up 150 80

Every event line starts with a game tick. An event is delivered at the
first poll of the event queue (`kGetEvent`) at or after its tick, in file
order.

| Kind | Data | Event |
|---|---|---|
| `key` | character [modifiers] | A key press with the SCI character code (decimal; for example 13 for Enter, 27 for Escape, or 18432 for the up arrow) and modifier bits (default 0). |
| `text` | the rest of the line | One key press per character, then Enter: a typed command. |
| `down`, `up` | x y [modifiers] | A mouse button press or release at (x, y), in screen coordinates (320 x 200). The modifiers say which button: 0 for the left, 3 (Shift) for the right, 4 (Ctrl) for the middle. |
| `move` | x y | Moves the mouse without an event. |

## Trace files

One line per kernel call, in the order the scripts make them:

    <call> <tick> <name>(<arguments>) = <result>

- `call` counts the kernel calls from 0; `tick` is the game tick when the
  call returned.
- `name` is ScummVM's kernel function name, without the `k`. Kernel calls
  with subfunctions use the subfunction's name (`DoSoundInit`), and the
  subfunction number is left out of the arguments.
- Arguments and result are values: numbers (segment 0) in unsigned decimal,
  and pointers as `<segment>:<offset>` with the segment in hex and the
  offset as four hex digits. Since the interpreter keeps ScummVM's segment
  model, pointers match as well.
- A call that a ScummVM workaround skips is traced with the value it leaves
  in the accumulator.
