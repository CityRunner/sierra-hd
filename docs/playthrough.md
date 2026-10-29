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

## Act II (done)

Stop tick 111600 (Act III starts at tick 110800 in the secret passage 49).

Act II's story runs on global 154 (0 to 4) and timer 368, found in the
scripts of rooms 10, 31, 32, 34 and 36 and regions 384 and 402:

| Bit | Scene | Room: script | Precondition | Tick |
|---|---|---|---|---|
| 4 | Celie and Lillian in the kitchen | 35: region 236 | none (global 216 set, as always after a room starts) | 72850 |
| 1 | two guests talk outside the study ("I can't go on with this any longer!") | 10: the room's script | global 154 is 0; sets it to 1 and starts timer 368 (1800 cycles) | 85100 |
| 2 | Clarence and Gloria at billiards | 36: region 245 | global 154 is 4 or timer 368 has run out (then 154 becomes 4) | 97750 |
| 8 | the doctor reading, seen through the peepholes | 320 (the view through the holes of passage 49 into the library 32): region 402 | global 154 at least 4 | 108000 |

Leaving room 10 by any way but the study door sets global 154 to 3 (the
study's region 384 would set 2 or 3; the library shows a scene at 2; the
dining room 34 loads region 237 at 3 and sets 4 when the timer runs out).
The route takes the short way: 154 goes 0, 1, 3, then 4 in the billiard
room.

- Room 47: arrow keys (right is the hallway, left along the landing,
  down the left staircase); clicks on the stairs do not move Laura.
- Room 37: off the foot of the stairs onto the bottom steps (they walk her
  to the floor); her father's advice (three messages and "Thanks,
  Daddy!") about 2400 ticks into the act; north under the landing into 33.
- 33, out of the French doors into the garden 4, east into 12 (Jeeves
  feeds Beauregard; clicks do nothing until he has gone), through the
  kitchen door into 35: event bit 4.
- Out to 12 (Jeeves goes into the cellar), back west through 4 behind
  the statue (the fountain blocks the lower path) into 10: event bit 1.
  The two guests stop in the study door and block it for good.
- Back to 4 (global 154 becomes 3), off its bottom edge into 33 and on
  into 37; wait there until timer 368 reaches 1 (about tick 95900).
- Along the back of 37 to the billiard room door (the way of Act I; the
  direct way is blocked by the control lines): event bit 2.
- Back into 33: "push the mirror" opens the east secret passage 50 (a
  detour, out again), "pull the clock" the west passage 49. On the
  platform under the portrait, facing it, "look through the holes": the
  view 320 of the library, event bit 8, then the "Act III" card. Back in
  49 the room change starts Act III (tick 110800).

`tests/playthrough/act-saves.txt` saves Act III at tick 111200 (no
message on screen).

## Act III (in progress)

Findings from the scripts (every write of global 118 is `lsg 118; ldi
<bit>; or; sag 118` in a script's state 0, guarded by global 216; 44 such
setters in all):

- **Peepholes in Act III.** Room 49's "look through the holes" (script 49
  at 0f98) picks a view room by act, Laura's loop (3, facing the wall) and
  which hole (room selector 262: 33 is the library hole by the platform).
  For Act III (global 165 is 2): the library hole gives room 324, which
  sets bit 8 with no other condition (324 at 01a2); the upper hole gives
  353 or 350 (global 173 bit 64), and 350 sets bit 8 too. Room 50's holes
  (script 50 at 1152): in Act III, Laura facing loop 2 at hole 43 gives
  room 333, which sets bit 4 (333 at 03dd), sets global 173 bit 8 and
  timer 370 to 1.
- **Bit 2: the attic.** Room 74's `init` loads region 261 in Act III (74
  at 033b; the docs had it under Act VIII). Region 261 plays its scene
  (bit 2, 261 at 0280; it also sets global 173 bit 8 and timer 370 to 2)
  when bit 2 is clear and timer 370 is 1. Timer 370 was started in Act II
  by the kitchen scene (region 236, 1800 cycles) and is 1 from long before
  Act III, so the attic scene waits from the act's start.
- **Bit 8: the billiard room.** Room 36 loads region 233 in Act III with
  no condition (36 at 0268).
- **Bit 1: room 63**, region 257 (63's `init` in Act III).
- **Bit 4 outdoors:** region 202 (acts I to IV, rooms 3 to 27) sets bit 4
  (202 at 0470) and runs timer 368.
- Bit-2 setters 301, 323, 335 and 354 are the views of rooms 50 and 49 in
  Acts I, II, IV and V, not Act III. Room 14 clears bit 4 only in Act VII.
- A first attempt's route (not kept) reached tick 132900 in room 14
  (outdoors, the playhouse) with bits 4 and 8.
- **Tried at the start of Act III:** "look through the holes" on the
  platform of 49 facing the portrait shows the library empty ("There is
  nothing of interest in the library", room 350 without its scene);
  facing another way gives "Try facing either set of holes." The view 324
  (Gloria at the Victrola, bit 8) needs the other set of holes (Laura not
  facing north, room property `east` 33), not found yet.
- **The attic from room 47:** room 47's `doit` sends Laura to 74 when she
  stands on control colour 4 (`onControl` 16) facing north, at the foot of
  the stairs on the right of the landing. In 1x screen coordinates the
  strip is x 242-266, y 123-128; game coordinates are 10 pixels higher
  (the picture port starts at y 10), so a click at (254, 126) walks her
  onto it (game y 116) from (254, 133). Colour 1 (game y 103-112, x
  229-267) is the way to room 48, colour 2 the way to 46.
- Kernel `OnControl` takes game (port) coordinates, the `--screens`
  control maps are screen coordinates: add 10 to y when reading them.
- Room 48 in Act III (48 at 01cd): region 259 (Clarence and Rudy argue,
  bit 4) while bit 4 is clear; it starts timer 370 at 1800 cycles, which
  would hold the attic scene back, so the route takes the attic first.

## Planned route of the later acts

From the regions that set the bits of global 118 and the act each room's
`init` loads them in. Each act's task confirms its four events with
`--globals` before writing the route; this list is a starting point.

| Act | Candidate events (room: region, bit) |
|---|---|
| III | billiard room 36: region 233 (bit 8, its Act III case); room 63: region 257 (bit 1); region 202 outdoors (Ethel, bit 4, acts I to IV); bit 2 not yet found (setters 301, 323, 335, 354 and the 300 scripts to check) |
| IV | region 203 outdoors (Clarence, bit 8); Laura's room 44: region 265; the study 31: region 264 (bit 2) |
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
- **Scenes take control.** While a scene runs (Jeeves in room 12, the
  guests in room 10), clicks and arrow keys do nothing, though typed
  commands still work; the route waits for the scene to end.
- **Control lines and actors.** Some ways are only open from one side or
  along one line (the staircases and the billiard room door of room 37,
  the back path of room 12); guests that stop in a doorway close it (the
  study door in Act II). Clicks that do nothing are read with
  `sci-vm-trace --actors` (Laura is the actor `1:016c`) and the control
  map of `--screens`.
- **Messages at the start of an act.** Her father's advice comes about
  2400 ticks into Act II; a click while it is up closes it instead of
  walking.
- **Peepholes count.** Looking into a room through the secret passages'
  holes loads that room's regions (room 320 for the library), so a scene
  seen that way is witnessed.
- **Deaths.** Some places kill Laura (the bayou, the attic, Act VII and
  VIII dangers). The route keeps away from them; a death ends the replay
  with the death message, which the checkpoints catch.
- **Random events.** The seed is 0 in both engines, so random choices are
  the same in every run; a change earlier in the replay can change them
  later, so later acts are written after the earlier ones are fixed.
- **Length.** The whole game will be several hundred thousand ticks. The
  release build of `sci-vm-trace` runs Act I in seconds; the reference run
  is the slow part.
