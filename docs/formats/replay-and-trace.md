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
  - `kGetTime` (elapsed ticks) adds 1 ms before it reads the clock when the
    kernel call just before it was also `kGetTime` (elapsed ticks): scripts
    that wait by reading the time over and over see it pass;
  - every other kernel call takes no time.
  A game tick is 1/60 s of virtual time, as `kGetTime` counts it.
- **Music timer.** The music player runs once per base tempo of the sound
  driver (4 ms for AdLib), on the virtual clock, in the interpreter's
  thread. Its timing, signals and cues therefore depend only on the
  virtual clock.
- **Random numbers.** The random source of ScummVM (`Common::RandomSource`)
  is seeded with the seed of the run (default 0) when the game starts or
  restarts.
- **Date and time.** The day starts at Monday 1990-01-01, 12:00:00 and runs
  with the virtual clock: `kGetTime` reports noon plus the virtual seconds
  (the game times scenes by the time of day).
- **Screen updates.** ScummVM's screen-update throttle (`updateScreen`, at
  most every 1000/60 ms, in every event read and after every `kAnimate`)
  and the text-edit cursor blink run on the virtual clock.
- **Stopping.** At the stop tick or call count the reference calls
  `quitGame()`: the game gets a quit event at its next poll of the event
  queue, and the next screen update that is due (an event read or
  `kAnimate`) aborts it after that kernel call. sci-vm does the same (and,
  should the game never poll, stops a minute of virtual time later).
- **Input.** Only replayed events reach the game; host input is ignored
  except for quitting. The mouse position is virtual: it starts at the
  centre of the screen (160, 100), follows replayed mouse events and is
  moved by `kSetCursor`.

- **Screens.** With `SIERRA_HD_SCREENS=<folder>` (reference) or
  `--screens <folder>` (sci-vm-trace), the screen the player sees is
  written as `<tick>.ppm` (binary PPM, 320 x 200, EGA colours, without the
  mouse cursor) whenever virtual time passes and the tick has reached the
  next multiple of `SIERRA_HD_SCREEN_EVERY` / `--screen-every` (default 60).
  `tools/vm-trace/compare-screens.py` compares the two folders.
- **Maps** (milestone 5). At the same ticks, the 1x priority and control
  maps are written as `<tick>.priority.pgm` and `<tick>.control.pgm`
  (binary PGM, 320 x 200, one byte per pixel, values 0 to 15, maximum
  value 15). They are the maps the logic reads (ScummVM's
  `GfxScreen::_priorityScreen` and `_controlScreen`), not the shown
  screen: the picture's priorities and control lines, the views added to
  the picture, and the actors kAnimate leaves drawn (stopped ones, with
  their control rectangles). Moving actors are not in them, since kAnimate
  restores what was under them before it returns. `compare-screens.py`
  compares them as well.
- **Actors** (milestone 5). With `SIERRA_HD_ACTORS=<file>` (reference) or
  `--actors <file>` (sci-vm-trace), every kAnimate call writes its cast,
  in the format below. `tools/vm-trace/compare-actors.py` compares two
  such files.
- **Parser** (milestone 6a). With `SIERRA_HD_PARSER=<file>` (reference) or
  `--parser <file>` (sci-vm-trace), every kParse and kSaid call writes what
  the parser made of the input, in the format below.
  `tools/vm-trace/compare-parser.py` compares two such files.

## Replay files

Plain text (UTF-8, ASCII but for a few typed commands), one event per
line. The first line that is neither empty nor a comment (`#`) must be
`sierra-hd replay 1`.

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
| `text` | the rest of the line | One key press per byte (the file is UTF-8, so a character above 127 is several keys), then Enter: a typed command. The text starts after the white space that follows `text` (so it cannot start with a space); spaces inside and at the end are typed. `text` alone is an Enter. |
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

## Actor files

One line per cast entry of a kAnimate call, in drawing order (the cast
sorted by y, then z, then list order, as `GfxAnimate::makeSortedList`
sorts it), written right after that call's trace line:

    <tick> <call> <object> <x> <y> <z> <priority> <view> <loop> <cel> <signal>

    5004 39018 1:016c 139 141 0 10 11 1 0 2002

- `tick` and `call` are those of the kAnimate call's trace line, so a
  frame (all lines of one call) can be found in the trace.
- `object` is the cast member, as a pointer in the trace
  (`<segment>:<offset>`, the segment in hex).
- `x`, `y`, `z`, `priority`, `view`, `loop` and `cel` are signed decimal
  numbers: the values GfxAnimate drew the entry with, after `fill()` has
  corrected loop and cel and set the priority from y (unless the signal
  fixes it).
- `signal` is four hex digits: the signal as `restoreAndDelete()` reads
  it back at the end of the call (`0002` view updated, `0004` no update,
  `0008` hidden, `0010` fixed priority, `0400` hit an obstacle, `4000`
  ignores actors, and so on).
- A kAnimate call without a cast list, or one that stops before drawing,
  writes no line.

## Parser files

One line per kParse or kSaid call, in call order, written right after that
call's trace line:

    <tick> <call> parse "<input>" ok words=<words> tree=<tree> type=<n> claimed=<n>
    <tick> <call> parse "<input>" syntax words=<words> type=<n> claimed=<n>
    <tick> <call> parse "<input>" unknown word="<word>" type=<n> claimed=<n>
    <tick> <call> parse "<input>" empty type=<n> claimed=<n>
    <tick> <call> said <spec> <outcome> claimed=<n>

    4507 36521 parse "look" ok words=800:3e8 tree=(141,(13f,((141,(13b,((141,(12f,w3e8)),-))),-))) type=4 claimed=0
    4932 43572 parse "smell the bed" ok words=800:43a,020:001,100:83d tree=(141,(13f,((141,(13b,((141,(12f,w43a)),-))),((142,(13d,((145,(131,w1)),((141,(139,((141,(130,w83d)),-))),-)))),-)))) type=4 claimed=0
    4606 38883 said 03e8f5f800caf6f2083dff full claimed=1
    4551 37783 said f208feff skipped claimed=-

- `tick` and `call` are those of the call's trace line. kParse calls the
  game's `wordFail` or `syntaxFail` method, whose kernel calls come before
  kParse's own line.
- `input` is the string kParse got, quoted: `\"` and `\\` for a quote and
  a backslash, `\xHH` for any other byte outside 32 to 126.
- The outcome: `ok` (the grammar accepted the words), `syntax` (it did
  not: `syntaxFail`), `unknown` (a word is not in the vocabulary:
  `wordFail`; `word` is that word as the tokenizer saw it, in lower case,
  quoted the same way) or `empty` (no words at all).
- `words` are the words as the grammar got them, after the room's synonyms
  (kSetSynonyms) and the pronouns (replaced by the object of the last good
  parse): `<class>:<group>` in hex, separated by commas (a word with several
  meanings would list them separated by `|`; SCI0 keeps one).
- `tree` is the parse tree the Said specs are matched against (ScummVM's
  `Vocabulary::_parserNodes`), written from node 0: a branch as
  `(<left>,<right>)`, a leaf as its value in hex, a word as `w<group>`
  followed by `/<right>` if it has a right child, a missing child as `-`.
- `type` and `claimed` are the event's selectors when kParse returns.
- `spec` is the Said spec's bytes in hex up to and including the 0xff that
  ends it, or `-` when there is none.
- The Said outcome: `null` (a null spec), `invalid` (a pointer that is not
  a string), `skipped` (no parsed event, or it is already claimed; the
  spec is not matched), `none`, `partial` or `full`; `claimed` is the
  event's flag as the call leaves it (`-` without an event).
