# The save format

sierra-hd's own saved games (spec section 9, milestone 8). The code is
`src/sci-vm/src/save_game.cpp` (the file), `state.cpp` (writing the state)
and `state_load.cpp` (reading it back). The state itself, its ten
components and what it leaves out are defined in `docs/ASSUMPTIONS.md`,
"Milestone 8: the VM state"; the decisions on restoring are in
"Milestone 8: the save format".

## Layout, version 1

All integers are little-endian; a string or a byte block is a `u32` length
followed by that many bytes (`StateWriter::string`, `StateWriter::bytes`).

| Field | Type | Contents |
|---|---|---|
| magic | 8 bytes | `SCIHDSAV` |
| version | `u32` | the format version: 1 |
| game | 32 bytes | the game identity (below) |
| description | string | what the player typed in the game's save dialog |
| year | `u16` | date of the save, e.g. 1990 |
| month, day | `u8`, `u8` | 1 to 12, 1 to 31 |
| hour, minute, second | 3 x `u8` | time of day of the save |
| ticks | `u32` | the play time at the save, in game ticks (1/60 s): what kGetTime(elapsed) returns (ScummVM's `playTime`) |
| program | string | the program that wrote it, e.g. `sierra-hd 0.1.0` |
| state | byte block | the VM state as `write_state` writes it |
| checksum | 32 bytes | SHA-256 of every byte before it |

- **Date and time** are the clock's local time: the host's in the program,
  the deterministic day (Monday 1990-01-01 12:00:00 plus virtual time,
  `docs/formats/replay-and-trace.md`) in `sci-vm-trace`, so deterministic
  runs write identical files.
- **The game identity** is the SHA-256, over the game's script and
  vocabulary resources in (type, number) order, of each resource's type
  (`u8`), number (`u16`) and data (a byte block; `0xFFFFFFFF` for one that
  cannot be read). The state's meaning depends on exactly these: the
  scripts' buffers and objects, the selector and kernel tables and the
  parser's words. Another release of the game, or patched scripts, give
  another identity. ScummVM checks script 0's size and the game object's
  offset instead (`gamestate_restore`).
- **The state** is the whole stream of `write_state`, all ten components
  with their names, so that the state hashes of a save can be computed
  from the file alone. The `execution` component is written but not
  restored (below).

## Slots and the save folder

- **Slots** 1 to 99, as ScummVM's (0 is its autosave slot, not used). The
  game sees slot n as the virtual id 100 + n (`SAVEGAMEID_OFFICIALRANGE`).
  sci-vm reads and writes saves through a `SaveStore` the host supplies
  (`sci/vm/files.hpp`); it does no I/O itself.
- **Files:** slot n is `sierra-hd-<nnn>.sav` (`sierra-hd-001.sav`), in the
  folder `saves` next to the program's configuration
  (`sci::platform::SaveFolder`). They are kept apart from the `files`
  folder, where the files the game writes itself go, so that a game file
  can never be taken for a save. The configuration's `saves` key names
  another folder (relative to the configuration's folder;
  `src/app/README.md`). `sci-vm-trace` takes `--saves <folder>`, by
  default a new empty folder per run, removed at its end.
- **The list** the game's dialogs read (kGetSaveFiles, ScummVM's
  `listSavegames`): every slot whose file starts with the magic and a
  readable prefix (magic, version, game identity, description, date and
  time) and has a description, newest first by the date and time in the
  header; saves of the same second by slot, increasing (ScummVM's sort
  leaves their order to the sort algorithm). At most 20 are listed, each
  description cut to 35 bytes, in 36-byte entries followed by a zero byte,
  and the slot ids (100 + slot) in the array the game passes. Other files
  are left out.
- **New saves** (kSaveGame with a virtual id below 100) take the first slot
  without a listed save; the same low id again, without a kGetSaveFiles in
  between, overwrites the last new save (ScummVM's `_lastSaveVirtualId`);
  an id of 100 to 199 overwrites that slot if it is listed and fails
  otherwise. The program named in a save is `sierra-hd <version>`, or
  `sci-vm-trace`.
- **kCheckSaveGame** is true only for a listed save that `read_save_header`
  accepts: this format version, this release of the game and the right
  checksum. Otherwise the game's own restore dialog says that it cannot
  restore that game (ScummVM checks only the version there, and shows its
  own message box for another release in kRestoreGame).
- **kRestoreGame** returns failure to the game, logging the message of the
  table below, for a missing save or a header that fails its checks;
  nothing changes. A failure while reading the state stops the game with
  the message as a fault. On success the host's time goes on from before
  the restore (virtual time, the wait under way and the music timer, which
  the state brought back as saved), and it does what ScummVM's
  `gamestate_restore` does after loading: the play time (kGetTime) is set
  back to the saved one, the last kWait and screen-update times are now,
  the 1x maps and the shown screen are cleared (`clearForRestoreGame`),
  the picture window is set again (`kernelSetPicWindow`), the GC
  countdown restarts, the
  restart flag becomes 2 (kGameIsRestarting reports "restored"), the event
  counter and the throttle are reset, the last new save is forgotten, the
  global pause count of the music is reset; every song that was playing is
  played again from its saved tick (`reconstructPlayList`,
  `soundPlay(restoring)`: the channels' set-up, programs and controllers
  go to the device again, notes are not started), or, if none was
  playing, the first paused one. Then it aborts the running code, the
  files are closed and the game object's `replay` method takes over
  (runGame, `kAbortLoadGame`).
- **The persistent hash** of "Milestone 8: the VM state" equals the one at
  the save right after `read_save`, before these fixups (the round trip of
  `--save-check` and the unit tests take it there); the fixups change the
  clock, the machine part, the 1x maps and the songs as ScummVM's restore
  does (`docs/ASSUMPTIONS.md`, "Milestone 8: saving in the game", "The
  clock on a restore").

## Reading

`read_save` checks, in this order, and stops at the first failure with a
message meant for the player (`sci::core::Error`):

| Check | Error code | Message |
|---|---|---|
| at least 12 bytes and the magic | InvalidData | This is not a sierra-hd saved game. |
| the version is 1 | Unsupported | This saved game has format version N; this program reads version 1. |
| room for the checksum | InvalidData | This saved game is truncated. |
| the checksum | InvalidData | This saved game is damaged (its checksum is wrong). |
| the header's fields | InvalidData | This saved game's header is truncated. |
| the game identity | Unsupported | This saved game was made with another release of the game. |
| the state block ends where the checksum starts | InvalidData | This saved game's state does not fit the file. |
| the state (below) | InvalidData | This saved game cannot be restored: *the inconsistency*. |

Up to the game identity nothing in the machine changes. The state is read
by a `load_state` per part, the counterpart of each `save_state`; every
count is checked against the bytes left, every index and reference (a
port, a segment, a song in the play list, a parse tree node, a table's free
list) against what it refers to, and the sizes the engine relies on (the
1x maps, the stack, a script's buffer) against the game's. A part takes the
values it read only when all of them are consistent, but a failure in a
later part leaves the earlier ones replaced, so a machine whose state fails
to load must not run on: kRestoreGame stops the game with the message as a
fault.

## What a restore rebuilds

As ScummVM's `gamestate_restore` does after loading:

- **Scripts** are loaded again from the resources (with the script
  patches), which finds the exports, synonyms and locals block in them
  again; the saved buffer (the patched, relocated resource with the strings
  the game wrote into it) then replaces the loaded one, after checking that
  it has the same size. The objects come back whole from the save.
- **Segments**: the segment table, the script-to-segment map, the class
  table and the clone, list, node and hunk tables with their free lists
  are rebuilt from the stream; each script is reconnected to its locals
  segment. The stack is recreated at its size and cleared, as ScummVM
  does (its contents belong to `execution`).
- **Globals and stack**: the machine's pointers to the stack and to script
  0's locals (the globals) are reconnected (`initGlobals`,
  `reconstructStack`).
- **Ports**: the window manager and menu ports, every window and every
  freed window still current are recreated, then the picture window, the
  current port, the visual order and the id table are pointed at them; the
  menu's return port is found by its id. The current font follows the
  current port's font id at its next use.
- **Songs**: each song's resource is parsed again from the game files by
  its number; its MIDI parser comes back with the saved (channel-filtered)
  data and its exact position, timing, volumes and held notes, so the songs
  play on from where they were. The songs that were playing before the
  restore get their notes switched off on the device. kRestoreGame then
  plays the restored songs again from their tick, as above.
- **Cursor, menu bar, parser**: the cursor's shape (checked against the
  game's cursors) and visibility, the menus and items, the parser's
  synonyms, last parse tree and pronoun come back as saved.
- **Screen**: the 1x visual, priority and control maps and the picture
  stack are restored; what is shown is not part of the state, and the
  game's `replay` method redraws the room (ScummVM clears the screen and
  the game redraws it the same way).

## Versions

Version 1 is the first. In every version the file starts with the magic,
the version, the game identity, the description and the date and time as
above, so that the save dialogs can list a save of any version (and the
game then says it cannot be restored). When the format changes, the version goes up and
this document gets a section per older version saying how a file of that
version is migrated to the current one (a documented step that rewrites
the old stream into the new one before reading it). An older version for
which no migration is written, and every newer version, is refused with
the message naming both versions (table above). Any change to what
`write_state` writes is a format change.

## Tests

- `tests/unit/vm/save_test.cpp`: on a synthetic machine (script 0, the
  stack, a list, a hunk, a song; no game data), a save read into a fresh
  machine gives the same persistent hash and the same hash per component
  but `execution`; the header comes back as written; each error of the
  table above, every truncation of the state (with its length and the
  checksum made right) and bytes after the state are reported.
- `tests/unit/vm/save_kernel_test.cpp`: the four kernel functions on the
  synthetic machine with saves in memory: slot choice and overwriting, the
  list's order and its limit of 20 and 35-byte names, a restore that
  brings the state back, aborts with LoadGame and sets the restart flag to
  2, a restore that keeps virtual time running and sets the play time,
  the last wait and the throttle as ScummVM does, and saves of another release, of another version, damaged or not
  saves at all returning failure with nothing changed, and a state that
  fails to load stopping the game.
- `tests/unit/platform/save_folder_test.cpp`: `SaveFolder`'s slot names,
  writing and reading back, the slot list in order, a missing folder,
  files and folders that are not slots, a folder that cannot be written,
  and the configuration's `saves` key.
- `sci-vm-trace --save-check` (`tools/vm-trace/README.md`) makes the round
  trip with the real game at chosen ticks.
- `tests/fuzz/fuzz_save.cpp` (`fuzz-save`) reads arbitrary bytes as a save
  into the synthetic machine; an odd first byte appends the right
  checksum, so that mutations reach the state loader. Its seeds in
  `tests/fuzz/corpus/save` are saves of the synthetic machine, written by
  `SCI_WRITE_SAVE_SEEDS=tests/fuzz/corpus/save build/debug/tests/unit/sci-vm-tests`.
