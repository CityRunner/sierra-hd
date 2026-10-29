# Milestone 8: saves

Status: **approved** (2026-10-29), as is.

Exit criterion (spec section 12): saving and restoring at a set of scenario
points gives a VM state with the same hash as before saving, and replay
continues identically. Spec section 9: the project's own save format works
for the whole game. It is versioned and described in `docs/`, and saves
from an older format version are migrated by a documented step or
rejected with a clear message.

The decisions are in `docs/ASSUMPTIONS.md`, in the sections "Milestone 8:
..." (the VM state, the save format, saving in the game, round trips, more
in-game restores, restores on the MT-32). The format is in
`docs/formats/save-format.md`.

## What was implemented

1. **The VM state and its hash.** The VM state is defined in ten parts:
   - execution;
   - segments;
   - machine;
   - clock;
   - random;
   - events;
   - files;
   - parser;
   - sound;
   - graphics.

   Each part writes itself in a canonical byte stream, free of pointer
   values and allocation order, and is hashed with SHA-256. The
   **persistent** hash covers every part but `execution`; it is what a
   game restore must reproduce. A restore re-enters the game through its
   `replay` method, as in SCI, so the stack is not restored.

   Left out, with the reasons recorded:
   - what is rebuilt from the game files (resources, vocabulary, caches);
   - presentation (the shown screen, the HD layers);
   - the audio device and synthesiser internals;
   - host input.
2. **The save format, version 1** (`docs/formats/save-format.md`).
   - **Header:**
     - magic and format version;
     - the game's identity: a SHA-256 of all script and vocabulary
       resources;
     - the description, date and time, play time and program version.
   - **Body and checksum:** the state stream follows, then a SHA-256
     checksum.
   - **Size:** saves are 490–940 KB.
   - **Refused with a clear message:**
     - another release of the game;
     - another format version (both versions named);
     - a damaged or truncated file;
     - an inconsistent state.

     Nothing in the machine changes before the identity check passes.
   - **Versioning policy:** an older version is migrated by a step
     documented with the next version, or refused. A newer version is
     always refused.
3. **The loader** rebuilds what the save does not hold, as ScummVM does
   after a restore:
   - scripts reloaded from the game files with their saved buffers;
   - the heap and tables;
   - ports and windows reconnected;
   - songs re-parsed at their saved positions;
   - the cursor and the menu bar.
4. **Saving in the game.**
   - **Kernel calls:** kSaveGame, kRestoreGame, kCheckSaveGame and
     kGetSaveFiles work on the format, through the game's own File menu
     dialogs.
   - **Slots:** 1–99, listed newest first. At most 20 are shown, with names
     cut to 35 characters, as in ScummVM.
   - **Saves that cannot be restored** are listed but refused, and the
     game's own dialog says so.
   - **Where saves go:** `saves/sierra-hd-<nnn>.sav` next to the
     configuration. The new configuration key `saves = <folder>` changes
     the folder.
   - **A restore follows ScummVM's restore:**
     - the host's time, random source and music timer keep running;
     - the game's play time goes back to the save's;
     - what ScummVM resets is reset, and the hunks and system strings are
       allocated again;
     - the picture window is set again;
     - the device gets the same switch, volume, reverb and note-off
       messages, and the songs that were playing start again with their
       instruments.
5. **Round trips** (`tools/vm-trace/run-round-trips.sh`). At five points
   in every scenario run:
   - write a save, including the execution;
   - load it into a fresh machine built from the game files;
   - compare the hashes;
   - continue the run in the fresh machine;
   - compare everything from that point on with the uninterrupted run.

   This is the strongest test that a save holds everything that drives
   future execution. The game's own restore path is tested separately by
   the in-game scenarios against ScummVM.
6. **The reference** (patch 0003). In deterministic mode, ScummVM's
   restore takes its wait and screen-update times from the virtual clock,
   and its play time follows `setTickCount`. Each scenario run now gives
   each engine its own empty save folder and its own reference settings.

## Results

### Scenarios: both devices, all comparisons

All 38 runs of `tests/scenarios/scenarios.txt`, including 5 new save
scenarios, were run in the reference and in sci-vm on each device. They
ran in the main checkout after the last merge, with the reference rebuilt
from the current patch. All 76 runs match on everything: traces, screens,
1x maps, actors, parser, layout, and the OPL log on AdLib or the MIDI log
on the MT-32.

The save scenarios, and two earlier runs for comparison:

| Scenario | Stop tick | Kernel calls | OPL writes (AdLib) | MIDI messages (MT-32) | Round trips |
|---|---|---|---|---|---|
| boot | 3,600 | 12,022 | 1,286 | 417 | 5, identical |
| intro | 30,000 | 227,303 | 81,604 | 14,638 | 5, identical |
| **save-restore** (new) | 6,500 | 49,510 | 1,040 | 1,292 | 5, identical |
| **attic-restore** (new) | 14,500 | 131,124 | 1,040 | 7,287 | 5, identical |
| **outdoors-restore** (new) | 13,600 | 127,466 | 4,854 | 3,456 | 5, identical |
| **inventory-restore** (new) | 25,000 | 240,704 | 5,888 | 4,206 | 5, identical |
| **restart-restore** (new) | 9,000 | 71,489 | 1,356 | 2,120 | 5, identical |

What the save scenarios do:
- **save-restore:** saves in Laura's room, walks away, restores, walks on.
- **attic-restore:** two saves, "Stairway" in room 74 and "Fifi's room".
  Both engines list them newest first. Each is restored in turn, and each
  restore redraws the stack of picture 74 over picture 174 that matches its
  save: `74.palette1+174.palette1` and `74+174`.
- **outdoors-restore:** saves outdoors in room 21 while the music plays and
  restores in room 10. The music resumes identically.
- **inventory-restore:** saves with the soup bone, gives it to the dog, and
  restores. The inventory shows the bone again.
- **restart-restore:** saves, restarts with F9, and restores. The game
  offers no restore on the title screen or during the "Act I" card ("You
  cannot restore a game right now"), in either engine.

### The exit criterion: round trips at scenario points

All 38 runs, 188 round trips. `mouse` reaches only 3 of its 5 points.
Every one gives:
- **the same hashes:** the persistent and total state hashes are the same
  before saving and after loading into a fresh machine;
- **the same continuation:** the run then continues identically to the end
  (kernel trace, screens and maps, actors, parser, layout, OPL, and the
  state hashes at the later points).

Saves are 491–938 KB. The points fall inside parsing, menus and dialogs,
windows, walks, the intro's music, and before and after in-game restores.

- **ARM64:** saves are byte-identical on x86-64 and on ARM64 under
  qemu-user, and a save from either loads on the other with the same
  hashes.
- **Golden hashes:** all picture golden hashes match, including the room
  74 stacks recorded in milestone 5, which a restore in room 74 now uses.

### Bugs found and fixed on the way

- **Missing from the state:** the owner and priority-source maps. Saved
  screen areas under windows carry them, so they are now part of the state.
- **Restored from the save but not by ScummVM:**
  - the hunk table and the system strings, which ScummVM allocates again;
  - the random source and the music timer count, which belong to the host.
- **Missing from our restore:**
  - re-setting the picture window;
  - the device messages ScummVM's restore sends, which are audible on the
    MT-32 as channel volumes.
- **The program's save folder** crashed on a folder named like a slot.
- **The test script:** parallel reference runs shared one settings file,
  so one run could see another run's saves. Each run now has its own.

## Tests and style

- **Local checks.** `scripts/pre-push.sh` passes, run step by step:
  - clang-format, clang-tidy, the repository policy and the hint check;
  - all test suites in the release, ASan+UBSan and TSan builds;
  - fuzzing, including the new save loader target;
  - all tests on ARM64 under qemu-user.
- **New unit tests:**
  - the state hash, where changing any one part changes exactly that
    part's hash;
  - the format's header, and every refusal: magic, version, truncation,
    checksum, another game, a renamed part, every truncation of the state;
  - the kernel calls: slot choice and overwriting, the list's order and
    limits, a restore with the restart flag, refusals, a state that fails
    to load;
  - a restore keeping the host time and setting the play time back;
  - the device messages on restore;
  - a resume with the execution;
  - the save folder and the `saves` key.
- **Fuzzing:** the loader has a fuzz target, `fuzz-save`.

## CI minutes

**The gate is green** (run 40815800525, commit `a53e592`). It ran:
- style and static analysis;
- Linux x86-64 and ARM64 in release, ASan+UBSan and TSan;
- the fuzzers, including the new save loader;
- Windows x64 and ARM64.

macOS is optional and was not run.

Measured with `scripts/ci-minutes.py --branch milestone-8`:

| Platform | Jobs | Minutes | Included minutes | List price |
|---|---|---|---|---|
| Linux x86-64 | 6 | 59 | 59 | $0.35 |
| Linux ARM64 | 3 | 22 | 22 | $0.11 |
| Windows x64 | 1 | 12 | 24 | $0.12 |
| Windows ARM64 | 1 | 4 | 8 | $0.04 |
| **Total** (1 run) | | **97** | **113** | **$0.62** |

- **Cost:** none. The repository is public, so standard runners are free.
- **Runs:** one, the gate run of this pull request.
  - Every earlier push of `milestone-8` was checked locally first, and
    pushes start no run.
  - This report's commit changes only documentation and starts no run.

## Deviations from the spec

None.

## Open questions

None for the gate. Two details still follow the save rather than
ScummVM's restore, and no scenario reaches them:
- the global reverb after a restore;
- the order numbers of restored songs.

The milestone 9 playthrough will show whether they matter.
