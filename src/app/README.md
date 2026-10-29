# app

The program: window, input, presentation, configuration, saves and the
analysis cache. It is the only place (together with the audio output in
sci-audio) that uses the window system, OS files or threads, and it supplies
the libraries with their log sink, assertion handler and task-execution
interface.

- `platform/` (library `sci-platform`): the parts without a window, which
  the development tools use too:
  - the thread executor (it checks the floating-point profile in every
    worker);
  - reading the game folder and identifying the release by its hashes;
  - the configuration file `sierra-hd.ini`;
  - loading the hint files.
- `src/`: the program `sierra-hd`. At release v0.1 (built at milestone 3a,
  released after 3b) it is the room viewer in an SDL3 window:
  - `viewer.cpp` composes the 320N x 200N frame (pictures in HD or original
    mode, views with their animation, fonts) and tracks the changed
    rectangles;
  - `main.cpp` holds the window, the keys and the presentation, uploading
    only the changed rectangles to the texture;
  - `audio_output.cpp` plays the music (milestone 7): the AdLib driver
    with the game's patch.003, or the MT-32 driver on the user's ROMs with
    the game's patch.001, as the player's device; after each music timer
    call the synthesiser's samples (49716 or 32000 Hz) are mixed to the
    device rate on the main thread and queued in an `SDL_AudioStream`,
    which SDL's audio thread plays. HD pictures are made on a thread of
    their own (`HdPictures::request`), so that the music and the game go
    on at room entry: the frame shown stays for up to a second, then the
    new picture is shown at 1x until its HD images are ready.

## Running

    sierra-hd [game folder]

Without a folder argument, the folder comes from `sierra-hd.ini` next to the
executable; on the first start, the program asks for it. The hint files are
read from `hints/` next to the executable. `sierra-hd --help` lists the keys.

## Configuration

`sierra-hd.ini` (section `[sierra-hd]`, `format = 1`; the program writes
it back on exit):

| Key | Values | Default |
| --- | --- | --- |
| `game_folder` | a path | asked on the first start |
| `mode` | `hd`, `original` | `hd` |
| `scale` | `auto`, 1 to 16 | `auto` |
| `priority_snap` | `on`, `off` | `off` |
| `mt32_rom` | the folder of the MT-32 ROMs (see below) | empty |
| `music` | `adlib`, `mt32` (needs the ROMs, otherwise AdLib), `off` | `adlib` |
| `volume` | 0 to 100 (per cent) | 100 |
| `saves` | the folder of the saved games; a relative path is taken from the folder of `sierra-hd.ini` | empty: `saves/` next to `sierra-hd.ini` |

`music = mt32` plays on an emulated Roland MT-32 (mt32emu) and needs the
user's ROMs, which are never bundled: `mt32_rom` names their folder. Every
file in it ending in `.rom` (any case, at most 1 MiB) is identified by its
contents (mt32emu's list of known ROMs), so the names do not matter: the
MT-32 1.07 control ROM is preferred, then other MT-32 versions, then the
CM-32L, each with a PCM ROM of its model. Without a folder, or without a
usable pair in it, the program logs a warning and plays on the AdLib; the
chosen ROMs are logged too. The MT-32 changes the voices the game is told
(kDoSound GetPolyphony), not its timing, signals or cues.

`music = off` plays the music on the silent driver: timing, signals and
cues are unchanged, without sound or an audio device. Without an audio
device (or with `SDL_AUDIODRIVER=dummy`, as in CI), the program logs a
warning or plays into the dummy device; the game runs the same. SDL's
`disk` driver (`SDL_AUDIODRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=<file>`)
writes what would be played to a raw file (16-bit stereo at the rate it
reports on standard error); `tools/audio-gaps/find_gaps.py` lists the gaps
in such a file.

## Invariants

- Libraries never reach the OS directly; app passes them data and services.
- Game files are opened read-only.
- N is an integer; the rest of the window is letterbox (spec section 6.1).
