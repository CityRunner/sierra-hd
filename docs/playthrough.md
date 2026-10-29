# The playthrough (milestone 9)

Milestone 9 completes the game from the start to the ending with scripted
input in both engines (spec section 12). The input is one replay,
`tests/playthrough/playthrough.txt`, built one act per task. After each act
the whole replay so far runs from the start in sci-vm and in the reference
ScummVM, and every comparison of `tools/vm-trace/run-scenario.sh` must
match (traces, screens, maps, actors, parser, layout, OPL).

## How the clock moves

Found in the game's scripts (disassembled from the resources) and checked
with `sci-vm-trace --globals` on the replay.

- **The act is global 165**, from 0 (Act I, 8:00 PM) to 7 (Act VIII).
  `where.py` prints it as a Roman numeral (165 plus one).
- **Each act has four events, global 118**, one bit each (1, 2, 4, 8). An
  event is a scene Laura witnesses: a region script of the room (for
  example region 231, the Colonel and Fifi in room 42) sets its bit the
  first time she sees it and starts script 406, which shows the clock and
  plays its chime. Each event moves the clock 15 minutes.
- **The act changes on the next room change.** `game::startRoom` (script
  0) calls a procedure when global 118 is 15: it clears the act's state
  (globals 116, 117, 118, 172, 173, 178, 179, 180 and the 148 globals from
  225) and adds one to global 165. So the act ends in the room where Laura enters after the
  fourth event, not during the scene.
- **The room is global 11** (the previous room is 12).
- **Scenes depend on the act** (`lsg 165` in each room's `init` picks the
  region) and on per-act progress flags: global 173 (bits set as the
  guests move, for example bit 2 when Gertie leaves the parlor in Act I)
  and the timers 368 to 372, which `game::doit` counts down by one per
  game cycle (about 6 ticks) to 1. The billiard room uses its own state,
  global 199.
- The text of the clock does not wait on real time: the deterministic day
  runs with the virtual clock (`docs/formats/replay-and-trace.md`).

## Format and tools

- `tests/playthrough/playthrough.txt`: the replay (format of
  `docs/formats/replay-and-trace.md`), seed 0, with `#` comments for each
  act and room.
- `tests/playthrough/checkpoints.txt`: `<name> <tick> <act> <room>` lines,
  where the game must be at the end of each act.
- `sci-vm-trace --globals <ticks> <file>` writes every global at the ticks
  (`tools/vm-trace/README.md`).
- `tools/playthrough/where.py <globals file> [<checkpoints>]` prints the
  room, act and events at each tick and checks the checkpoints.
- `tools/playthrough/check.sh [<stop tick>] [<output folder>]` runs the
  replay to the last checkpoint in both engines with `run-scenario.sh`,
  checks the checkpoints in sci-vm, and removes its output when all holds.
- `sci-hd-frames <game> <folder> --at <ticks> --replay
  tests/playthrough/playthrough.txt --scale 1` shows where Laura is while
  writing the route.

## Act I (done)

Stop tick 63600 (Act II starts at tick 62840).

- Opening: "No" to "previously attended?" plays the whole opening (rooms
  777 to 783, then 209), about 27000 ticks; Act I starts in room 44.
- Room 44: the messages of the start, then out to 43 and 42.
- Event bit 8: the Colonel and Fifi in room 42 (region 231), tick 32750.
- Down the left staircase (47, 37) to the parlor 38.
- Event bit 2: Gertie and Clarence at the bar (region 217), tick 40400.
  It starts a timer (global 369, 1800 cycles).
- Event bit 1: Gloria and Rudy in the billiard room 36 (region 232), tick
  44600.
- Back in the parlor: "look at the parrot", "talk to gertie". After the
  timer ends (about tick 54700), a new visit to the parlor finds Gertie
  gone to bed (global 173 bit 2).
- Up the left staircase to 47 ("open the door": the closet), the first
  door of the hallway's west side into room 46.
- Event bit 4: Gertie asleep in room 46 (region 224), tick 62050.
- Out to 47: Act II, tick 62840.

## Planned route of the later acts

From the regions that set the bits of global 118 and the act each room's
`init` loads them in. Each act's task confirms its four events with
`--globals` before writing the route; this list is a starting point.

| Act | Candidate events (room: region, bit) |
|---|---|
| II | kitchen 35: region 236 (bit 4); parlor 38 and billiard room 36 (regions 245, 233); the grounds (region 381 in the outdoor rooms) |
| III | parlor 38 and billiard room 36 by their state; region 202 outdoors (Ethel, bit 4, acts I to IV) |
| IV | region 203 outdoors (Clarence, bit 8); Laura's room 44: region 265 |
| V | the Colonel's room 42: region 270 (bit 2); the gardens: regions 268 |
| VI | Laura's room 44: region 274 (bit 2); room 10: region 277 (bit 4); rooms 41 and 73: region 278 |
| VII | the grounds: region 281 (bit 1); Laura's room 44: region 385 (bit 4); room 69: region 256 |
| VIII | room 65: region 280; the chapel 58 (regions 256, 279, bit 8); the attic 74 (region 261, bit 2) |

Other setters by room script: 6, 10, 22 and 59 (bit 1), 22 (bit 8), 48
(bit 1), and the 300 and 400 scripts of the scenes. The game ends after Act
VIII whatever Laura found, so the route aims for the four events of each
act, not the score. Where it costs little, it adds rooms, typed commands
and inventory actions.

## Risks

- **Timers.** Some scenes wait on the timers 368 to 372 (game cycles), so
  a route that waits must wait long enough; the waits are in the replay as
  gaps.
- **Positions.** A click walks Laura to the first obstacle; a guest in the
  way stops her. Each new step is checked with frames before going on.
- **Deaths.** Some places kill Laura (the bayou, the attic, Act VII and
  VIII dangers). The route keeps away from them; a death ends the replay
  with the death message, which the checkpoints catch.
- **Random events.** The seed is 0 in both engines, so random choices are
  the same in every run; a change earlier in the replay can change them
  later, so later acts are written after the earlier ones are fixed.
- **Length.** The whole game will be several hundred thousand ticks. The
  release build of `sci-vm-trace` runs Act I in seconds; the reference run
  is the slow part.
