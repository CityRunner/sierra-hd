# Milestone 9: the playthrough

Status: **awaiting the customer's manual playthrough and approval.**

Exit criterion (spec section 12): the game is completed from start to
ending without errors:
- the agent's own scripted input, in both graphics modes;
- one full manual playthrough by the customer in HD mode;
- spot checks by the customer in original mode.

The optional state checkpoints against a reference walkthrough do not
apply: no reference walkthrough exists (spec section 12, "Optional, only
if...").

Decisions are in `docs/ASSUMPTIONS.md`:
- "Milestone 9: the reverb and song order on a restore";
- "Milestone 9: the playthrough in the program";
- "Small agent tasks";
- "Milestone numbers after A4".

The playthrough, act by act, is in `docs/playthrough.md`.

## What was done

1. **The two restore details left open by milestone 8** now follow
   ScummVM's restore: the global reverb, and the order numbers of
   restored songs. A two-songs scenario covers both; it matches ScummVM on
   both devices.
2. **The scripted playthrough** (`tests/playthrough/playthrough.txt`,
   seed 0) plays the whole game in one replay:
   - the opening and Acts I to VIII;
   - the good ending: Laura shoots Rudy in the attic, then the Colonel's
     epilogue, the sleuth rating and her notebook;
   - it clicks QUIT on the last screen, and the game ends by itself at
     tick 337613.

   It was built one act per task. After each act, the whole replay so far
   was run from the boot in sci-vm and in the reference ScummVM, and every
   comparison had to match.
3. **Tools** (`tools/playthrough/`):
   - `check.sh` runs the comparison and the checkpoints;
   - `where.py` prints the room, act and events at any tick, from the
     game's own globals (act = global 165, events = bits of global 118,
     room = global 11);
   - `act-saves.sh` makes a save at the start of each act through the
     game's own save dialog;
   - `program-run.sh` runs the program itself on the replay, headless.
4. **The program plays the replay**, with three new options:
   - `--replay <file>` and `--seed <n>`;
   - `--fast`, which changes only the pacing;
   - `--end-state <file>`, which writes the tick, globals and state hash
     when the game ends.

   A replay uses the deterministic time of day, as `sci-vm-trace` does.
   The game reads the time of day 32,222 times, and with the host's clock
   the replay went past the quit.
5. **Release candidate 0.9.0** for the customer's playthrough:
   - version 0.9.0;
   - the user guide that ships in the archive (`docs/USER-README.md`) now
     covers the game, saves, MT-32 ROMs and every configuration key;
   - the README and `docs/ARCHITECTURE.md` are updated;
   - release notes are in `docs/release-notes/v0.9.0-rc.1.md`.
6. **Undithering (milestone SU) was dropped** at the customer's request.
   Nothing of it is in the code.

## Results

### The scripted playthrough: sci-vm against ScummVM, boot to QUIT

| Comparison | Count | Result |
|---|---|---|
| Kernel calls (trace) | 2,448,724 | identical |
| Screens and 1x maps | 5,627 | identical |
| Actors | 50,061 frames | identical |
| Parser | 21 parses, 3,812 Said calls | identical |
| Window layout | 7,253 lines | identical |
| OPL writes (AdLib) | 215,334 | identical |
| Checkpoints (`checkpoints.txt`) | 38, the last one the quit | all hold |

### The playthrough in the program (both graphics modes)

`tools/playthrough/program-run.sh`, release build, headless (Xvfb), with
`--replay ... --seed 0 --fast`. Every run plays from the boot to QUIT and
exits with status 0, with no error or warning in its log. Every run ends in
`sci-vm-trace`'s end state: tick 337613, every global, and the same VM
state hash, total and per part.

| Mode | N | Music | Cache | Wall time |
|---|---|---|---|---|
| HD | 4 | AdLib | cold (176 scenes analysed in 2.4 s) | 29 s |
| HD | 4 | AdLib | warm | 26 s |
| original | 4 | AdLib | warm | 30 s |
| HD | 4 | MT-32 (customer's ROMs) | warm | 26 s |
| HD | 2 | AdLib | warm | 25 s |
| HD | 4 | AdLib, audio to disk | warm | 28 s |
| original | 4 | off | warm | 22 s |

I re-ran three of these after the last merge; they took 26 s, 26 s and
21 s, with the same results.

### For the customer's playtest

These files are local only, not committed:
- **Saves at the start of each act:** `sierra-hd-001.sav` to `-008.sav`,
  made through the game's own save dialog, named "Act I" to "Act VIII".
  Copy them into the program's `saves/` folder.
- **Two images of each act:** 16 images, in HD at N = 4, sent in the chat.

## Tests and style

- **Local checks:** `scripts/pre-push.sh` passes, run step by step:
  - style, clang-tidy, repository policy and hint check;
  - the release, ASan+UBSan and TSan builds with all tests;
  - fuzzing;
  - ARM64 under qemu-user.
- **New unit tests:**
  - the reverb and song order on a restore, and the driver reverb on a
    track's init (five tests);
  - the game's end-state accessors.

  The program's replay options are checked by the program runs above.
- **No compatibility layer remains** from porting (spec section 5.1).

## CI minutes

To be filled in from the gate run of this pull request.

## Deviations from the spec

None.

## Open questions

None for the gate. Not done, because it is not required: a run of the
replay in real time (about 94 minutes per run). The customer's manual
playthrough covers real-time play.

## What the customer needs to do

1. Download the 0.9.0-rc.1 build for your system from the GitHub
   pre-release.
2. Play the game through in HD mode, with the act saves if you like.
3. Spot-check some scenes in original mode with F12.
4. Report anything that differs from the original, or approve.
