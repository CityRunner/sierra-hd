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

## Act III (done)

Stop tick 154400 (Act IV starts at tick 153412 in room 14, outside the
playhouse).

Every write of global 118 in the scripts is `lsg 118; ldi <bit>; or; sag
118` in a scene script's state 0, guarded by global 216 (44 setters in
all, listed by bit with a small script over the disassembly). Act III's
four events, checked with `--globals`:

| Bit | Scene | Room: script | Precondition | Tick |
|---|---|---|---|---|
| 4 | Clarence and Rudy argue about Gloria | 48: region 259 (48 at 01cd) | bit 4 clear; starts timer 370 (1800 cycles) | 118450 |
| 8 | Gloria at the Victrola | 36: region 233 (36 at 0268) | none in Act III | 125900 |
| 2 | a man with Fifi in her attic room ("Stop eet! Leave me alone!!") | 74: region 261 (74 at 033b, 261 at 0280) | bit 2 clear and timer 370 at 1 when Laura enters 74 | 134050 |
| 1 | Ethel plays with the dolls in the playhouse | 63: region 257 | bit 1 clear | 148050 |

- **Timers are cleared at the act change**: the procedure of
  `game::startRoom` clears the 148 globals from 225, which include the
  timers 368 to 372. Timer 370 is 0 when Act III starts, so the attic
  scene needs it started in Act III, by the argument in room 48 (259) or
  through room 50's holes (view 333, which sets it to 1 at once; not used).
  Region 261 checks it only when Laura enters room 74: Fifi's room is
  empty if she comes too early, so the route waits in 47 until about tick
  132000.
- **The attic from room 47**: room 47's `doit` sends Laura to 74 when she
  stands on control colour 4 (`onControl` 16) facing north, at the foot
  of the stairs on the right of the landing; colour 1 at the east end of
  the hallway is the way into 48, colour 2 the way into 46.
- **Peepholes in Act III.** Room 49's "look through the holes" (49 at
  0f98) picks a view by act, Laura's loop and the room's property `east`
  (33 when she came in from 33): facing the portrait gives the library
  empty ("There is nothing of interest in the library", room 350); facing
  another way on the platform gives "Try facing either set of holes."
  The view 324 (Gloria at the Victrola, bit 8) belongs to the other set
  of holes, not found. Room 50's holes give view 333 in Act III (the
  argument of 48, bit 4, Laura facing south with `west` 43), not tried.
- Bit-2 setters 301, 323, 335 and 354 are views of rooms 50 and 49 in Acts
  I, II, IV and V; room 14 clears bit 4 only in Act VII (when Laura comes
  out of 63).

Route:

- 49: off the platform, east out of the clock into 33, out of its bottom
  edge into 37, up the left staircase into 47, along the hallway into 48:
  event bit 4.
- Back to 47, down the left staircase with the arrow keys (as in Act
  II), along the back of 37 into the billiard room 36: event bit 8.
- Back to 37, up into 47, to the foot of the right stairs and wait for
  timer 370; north into the attic 74, up the stairs and right into Fifi's
  room: event bit 2.
- Down to 47, down the left staircase, through 37 and 33 into the garden
  4, west into 10, 3 and 14. In 14 Laura arrives behind the playhouse at
  (276, 100) and must go round its right side ((268, 108), (285, 110),
  (285, 165)) to the door at (221, 130). "knock on the door": "Is that
  you, Laura? Come in and join me." Enter, and she goes into 63: event
  bit 1, then the "Act IV" card. Out of the door into 14: Act IV (tick
  153412).

`tests/playthrough/act-saves.txt` saves Act IV at tick 154000 (no message
on screen).

New risks found:

- **Control maps and coordinates.** Kernel `OnControl` takes game (port)
  coordinates; the `--screens` control maps are screen coordinates, 10
  pixels lower (the picture port starts at y 10). A click walks Laura to
  10 pixels above the point clicked, so the click lands on the screen row
  of the target in the map.
- **Wide base.** Laura's base rectangle is about 20 pixels wide: in tight
  places (behind the playhouse in 14) a straight walk stops at once
  (`CanBeHere` 0 in the trace) and the route goes in steps.
- **Scenes that check on entry.** Region 261 decides on the room's init;
  a timer running out while Laura is already in the room does not start
  the scene.

## Act IV (done)

Stop tick 170800 (Act IV starts at tick 153412 in room 14, the room's
picture after the playhouse; Act V at tick 169510 in room 21).

Ethel, Rudy and Clarence walk the grounds on timers, each a region loaded
in the outdoor rooms, with a table of rooms in its locals (read from the
scripts' local blocks). The four events, checked with `--globals` (the
tick is the clock's `ScriptID(406, 0)` in the trace):

| Bit | Scene | Room: script | Precondition | Tick |
|---|---|---|---|---|
| 4 | Ethel comes by, drunk | 3: region 202 (`ethelActions`, setter at 0470) | Laura in the room of Ethel's walk when timer 368 is a multiple of 100 | 155206 |
| 1 | Rudy comes by | 3: region 281 (`rudyActions`, loaded in Act IV while global 114 is below 2) | Laura in the room of Rudy's walk when timer 369 is a multiple of 100 | 155662 |
| 2 | Lillian in the study ("Oh, Laura... you startled me!") | 31: region 264 (setter at 0224) | bit 2 clear (room 31 loads 264 only then); none else | 159600 |
| 8 | Clarence and Rudy fight by the statue | 22: the room's script (`fight`, setters at 05d7 and 05fc) | on entry: global 114 or 115 is 8, or timer 371 or 369 is 1, and global 114 is not 10; sets bit 1, or bit 8 when bit 1 is set | 165100 |

The walks (timers count down one per game cycle, about 6.06 ticks, and
stop at 1; all are 0 when the act starts):

- **Ethel, region 202, timer 368, global 113** (rooms 3, 4, 5, 9, 10, 13,
  14, 21, 25 to 27, Acts I to IV). A 202 room's `init` sets the timer to
  1099 (113 to 0) when it is 0, to 900 (113 to 2) when it has run out.
  Her `doit` shows her when the timer is a multiple of 100 and Laura is in
  room `local72[timer / 100]` (not room 4): 1000 room 4, 900 room 10, 800
  room 3, 700 room 9, 600 room 13, 500 room 25, 400 room 26, 300 room 27,
  200 room 21, 100 room 14. Entering room `local72[11 - g113]` while the
  timer is above (10 - g113) * 100 shows her too (she showed again in 10
  at tick 162600, no event as bit 4 was set). At Act IV's start room 14
  sets the timer to 1099.
- **Rudy, region 281, timer 369, global 114.** The first 281 room sets the
  timer to 800 (114 to 0): room 3 at tick 155062 on this route. He shows
  at 700 in room 3, then 600 room 9, 500 13, 400 25, 300 26, 200 27, 100
  21 (`local54` = 22, 21, 27, 26, 25, 13, 9, 3, 10); then the timer stays
  at 1 (tick 161800 on this route), which opens room 22's fight. After the
  fight 114 is 10.
- **Clarence, region 203, timer 371, global 115** (rooms 6 while 115 is
  below 2, 7, 8, 18, 23, 29, 30 in Act IV; not visited on this route). The
  first 203 room sets the timer to 700 (115 to 0); entering room 18
  (`local42[7 - g115]`) while it is at least 600 shows him at once,
  otherwise he shows at 600 in room 6, 500 in 7, 400 in 8, 300 in 30, 200
  in 29 and 100 in 23 (`local42` = 22, 23, 29, 30, 8, 7, 6, 18). Seeing
  him sets bit 8 (setter at 02c2). When his walk reaches index 0 (room 22)
  the timer is set to 1. Bit 8 comes from the fight instead, as Rudy's
  bit 1 was already set.
- Act IV has a fifth way for bit 1 or 8 (the fight), so the route needs
  only three rooms of scenes: 3, 31 and 22.

Room exits (the room properties at 28, 30 and 32; for the outdoor rooms
these read as east, south and west, but entries differ: from 14 Laura
leaves north behind the playhouse and comes into 3 at its bottom right):
3: 10, 14, 9; 4: 12, 33, 10; 6: 7, 18, 12; 7: 8, 6; 12: -, 23, 17; 21:
22, 27; 23: 24, 29, 22; 24: 8, -, 23; 29: 30, -, 28.

Route:

- 14: round the right side of the playhouse ((285, 165), (285, 110),
  (268, 108)) and north off the top behind it into room 3.
- 3: Ethel (bit 4), then Rudy (bit 1), each with Enter for the clock; on
  the path at the bottom right, the right arrow key east into 10.
- 10: down the left side ((10, 110)) to the foot of the steps ((155,
  150)); the up arrow key walks her in through the French doors to the
  study 31 (typing "open the door" answers "Just do that yourself.").
- 31: the description, then Lillian (bit 2); Enter closes the messages
  (over by about tick 162000).
- The left arrow key out to 10, off its bottom edge into 21 (its
  description), the right arrow key east into 22 (its description): the
  fight (bit 8), the "Act V" card, Enter for the messages; the fight is
  over by about tick 168100. The left arrow key walks her west into 21:
  Act V (tick 169510).

`tests/playthrough/act-saves.txt` saves Act V at tick 170200 (room 21, no
message on screen).

New risks found:

- **The clock waits for a key.** Script 406 stays on screen after the
  chime, and its `handleEvent` eats every click and key until one closes
  it (Act III's route closed it with Enter without needing to). It is one
  object for every scene: if a second scene starts it while the first is
  still up (Ethel at 155206 and Rudy at 155662 in room 3), the clock never
  ends and every later click and key is ignored. The reference ScummVM
  does the same (the run to tick 157300 matched on every comparison), so
  it is the game's behaviour: the route closes each clock with Enter
  before the next scene can start.
- **Walks on timers.** Ethel's and Rudy's scenes need Laura in a given
  room at a given cycle, so a change of the route before them moves or
  loses them; check the timers with `--globals` (globals 113 to 115, 368
  to 372) when changing it.
- **Arrow keys at the edges.** Clicks at the right edge of room 3 stop at
  the fence; the arrow keys walk Laura off the edges and through the
  study's French doors.

## Act V (done)

Stop tick 202400 (Act V starts at tick 169510 in room 21, 12:00 midnight,
with all timers 0, global 114 at 10 and 115 at 12; Act VI at tick 201350
in the hallway 43).

Found in the scripts:

- Script 0's exports 5, 6 and 7 are Bset, Bclr and Btst on the flag
  bits from global 440 (flag n: global 440 + n / 16, bit n mod 16). They
  are not cleared at the act change.
- Global 187 is the quarter of the hour: script 406 adds one per event
  and wraps it to 0 after 3 (global 186 is the hour). So "global 187 is
  3" means after three events of the act.
- Room 42's `init` in Acts II to VIII (when global 109 & 48 is clear):
  region 270 (Act V; 240 in the other acts) when flag 38 is set, or when
  global 187 is below 3, global 123 bit 64 is clear and flag 37 is clear;
  otherwise, in Act V with global 187 at 3, its own scene (bit 4, setter
  at 079a).
- Region 270's `init` sets timer 369 to 1800 when it is 0; when that
  timer is 1 and bit 2 is set it starts another script, otherwise it sets
  flag 38 (Bset at 01e4) and starts the scene that sets bit 2 (0332).
- Rooms 43 and 49 clear flags 37 and 38 when Laura leaves them for a
  room other than 42 or 49 (43) or 42 or 43 (49). Region 270's flag 38
  would bring 270 back on every visit, so the room-42 scene needs Laura
  out of 43 into another room and back.
- Room 50's "look through the holes" (050 at 1215) in Act V, with Laura
  come in from 33 (the property `east`): facing up (loop 3) the view 306
  (scene34e, the dining room, setter at 0334), facing down the view 304
  (scene38e, the parlor, setter at 0378); facing right: "Try facing
  either set of holes."; off the platform: "You're not close enough."
  Neither has a precondition in Act V.

The four events, checked with `--globals` (the tick is the clock's
`ScriptID(406, 0)` in the trace):

| Bit | Scene | Room: script | Precondition | Tick |
|---|---|---|---|---|
| 1 | Rudy having dessert and coffee in the dining room | 306, the view through 50's holes (scene34e) | Laura came into 50 from 33 and faces up on the platform | 178954 |
| 8 | Clarence sitting alone at the bar of the parlor | 304, the view through 50's holes (scene38e) | as 306, facing down | 181447 |
| 2 | the Colonel and Fifi ("Laura! What are YOU doing in here?!", "But, Uncle Henri, I don't understand how you could...!") | 42: region 270 (setter at 0332) | global 187 below 3 and flags 37 and 38 clear, or flag 38 set | 190435 |
| 4 | the Colonel's room empty | 42: the room's script (setter at 079a) | global 187 at 3 and flag 38 clear (Laura has left 43 for another room since 270) | 197635 |

Region 272 in the parlor (bit 8) and view 355 through 49's holes (bit 4)
are other ways, not used. Region 268 sets no bit.

Route:

- 21: the up arrow key walks Laura north into 10, where she would walk on
  towards the study's French doors; a click stops her. Over the top left
  of 10 ((10, 120), (10, 70), (80, 55)) into the garden 4, behind the
  statue and off its bottom edge into 33.
- 33: "push the mirror" (at (62, 152)): she walks into the passage 50 and
  stands at (16, 110).
- 50: onto the platform at (121, 98), the up arrow key, "look through
  the holes": Rudy (bit 1). The views close by themselves; Laura is back
  on the platform at (121, 100). A click 3 pixels below her turns her to
  face down, and the same command shows Clarence (bit 8). She comes back
  at (131, 104), where a walk left stops at once; back to (121, 98), then
  down the steps to (16, 110) (two clicks, the first stops at (91, 109))
  and out at (8, 132) into 33 (a second click there would walk her into
  the library 32).
- 33 off the bottom edge into 37, up the left staircase into 47; off the
  top of the stairs to the right ((100, 147): a click up from there stops
  against the wall), onto the landing ((160, 125)) and north up the
  hallway into 43.
- 43: past the edge of the wall ((40, 175), (100, 150)) to the upper door
  on the left (control colour 5, (75, 140), (20, 140)) into 42: the
  Colonel and Fifi (bit 2). Enter closes the clock and the messages.
- Out of 42's east door into 43, south into 47 and straight back north
  into 43, then into 42 again: the room is empty (bit 4), then the "Act
  VI" card. Enter closes them; out of the east door into 43: Act VI (tick
  201350).

`tests/playthrough/act-saves.txt` saves Act VI at tick 201800 (room 43, no
message on screen).

Act V's route leaves room 21 at tick 170300, so the checkpoint
`act-5-start` moved from 170800 (Act IV's stop tick) to 170200.

New risks found:

- **Flags outside the act's globals.** Flags 37 and 38 (global 442) live
  across acts and depend on how Laura leaves rooms 43 and 49; a scene
  that waits for a flag to clear needs the right way out.
- **Views put Laura back elsewhere.** After the view 304 Laura stands 10
  pixels right of where she looked from, at a place where some walks stop
  at once; the route goes back to a known point first.
- **Clicks that change rooms.** A click near an edge of a small room can
  walk Laura out of it (33's left edge leads into the library 32); check
  the room after each step.

## Act VI (in progress)

Found in the scripts (Act VI is global 165 = 5):

- Room 44 (Laura's room) loads region 274 in Act VI; its scene sets bit 2
  (setter at 013b) when bit 2 is clear. No other precondition.
- Room 48 loads region 276 in Act VI; its `init` starts the scene that sets
  bit 8 (setter at 04b9) when bit 8 is clear.
- Room 10 loads region 277 in Act VI; its `init` starts the scene that sets
  bit 4 (setter at 0308) when bit 4 is clear.
- Room 6 (Celie's shack): knocking in Act VI with global 135 bit 256 clear
  shows "What you doin' here, girl?! ..." (text 6 16), sets the bit and
  starts the scene that sets bit 1 (setter at 0724); a second knock answers
  "Go away!!".

## Images

Two images per act for the customer, at ticks of the replay (one during a
key scene, one in another room with characters), rendered with
`sci-hd-frames <game> <folder> --at <ticks> --replay
tests/playthrough/playthrough.txt` (original | HD side by side):

| Act | Key scene | Another room with characters |
|---|---|---|
| I | 33300: the Colonel's room 42, the Colonel and Fifi ("Pardon, Henri! I got zee lipstick on you!") | 41000: the parlor 38, Gertie and Clarence at the bar ("Shhh. Here's that young friend of Lillian's.") |
| II | 98400: the billiard room 36, Clarence and Gloria ("Everything all right, Gloria?") | 73300: the kitchen 35, Celie and Lillian |
| III | 135000: Fifi's room in the attic 74 ("You'll regret that, you little vixen!") | 119000: room 48, Clarence and Rudy arguing |
| IV | 166500: room 22, Clarence and Rudy fight by the statue | 161200: the study 31, Lillian ("Oh, Laura... you startled me!") |
| V | 191200: the Colonel's room 42, the Colonel and Fifi ("But, Uncle Henri, I don't understand how you could...!") | 179800: through the holes of passage 50, Rudy in the dining room ("You see Rudy having dessert and coffee in the dining room.") |

## Planned route of the later acts

From the regions that set the bits of global 118 and the act each room's
`init` loads them in. Each act's task confirms its four events with
`--globals` before writing the route; this list is a starting point.

| Act | Candidate events (room: region, bit) |
|---|---|
| V | done (above) |
| VI | Laura's room 44: region 274 (bit 2); room 10: region 277 (bit 4); rooms 41 and 73: region 278; the attic 74: region 273 (global 123 bits 16 and 64); room 6's own script (bit 1, setter at 071f; its `init` checks Act VI); region 276 (bit 8, rooms 13, 14, 48 and 65; room 48 loads it in Act VI); through passage 50's holes when Laura came in from 43 (`west`): facing up the view 335 (scene44f, Laura's room, bit 2), any other way 334 (scene48f, room 48, bit 8). Act VI starts in the hallway 43 with flags 37 and 38 clear, timer 369 0 and global 187 at 0 |
| VII | the grounds: region 281 (bit 1); Laura's room 44: region 385 (bit 4); room 69: region 256 |
| VIII | room 65: region 280; the chapel 58 (regions 256, 279, bit 8); bit 2 to find (region 261 in the attic is Act III's) |

Other setters by room script: 6 (Act VI), 10 and 59 (bit 1), 22 (Act IV), 48
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
