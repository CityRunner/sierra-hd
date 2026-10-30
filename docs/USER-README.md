# sierra-hd 0.9

sierra-hd is a new interpreter for Sierra's *The Colonel's Bequest*
(1989), with an HD mode that redraws the backgrounds as sharp vector art
at any whole-number scale. The game itself plays exactly as the original.

**Release 0.9 is a release candidate.** The whole game plays from the
start to the ending, with music and sound, saving and restoring. It also
contains the room viewer of release 0.1.

## What you need

- Your own copy of *The Colonel's Bequest*, English DOS version (the GOG
  release works). sierra-hd contains no game data.
- One of:
  - Windows 10 or 11 (x64 or ARM64);
  - Linux with glibc 2.38 or newer, for example Ubuntu 24.04, Debian 13 or
    Fedora 39, on x64 or ARM64, with X11 or Wayland.
- For Roland MT-32 music (optional): your own MT-32 or CM-32L ROM files.
  Without them, the music plays on the AdLib.

## Starting

Unpack the archive anywhere and start `sierra-hd` (on Windows
`sierra-hd.exe`). The first time, it asks for the game folder: the one that
contains `RESOURCE.MAP`. You can also give the folder on the command line:

    sierra-hd "C:\GOG Games\Colonel's Bequest"

The program checks that the folder holds a supported release. The tested
release is the English DOS 3.5" floppy version 1.000.046. The 5.25"
version is accepted with a warning, and its pictures are drawn without the
HD hints.

The first start analyses all pictures for the HD mode, behind a progress
bar, and keeps the result in `cache/` next to the program; later starts
are quick.

## Playing

The game is played as the original: type commands (`look`, `open door`,
`talk to fifi`), walk with the mouse or the arrow keys, and use the menu
bar (Escape) or the game's function keys, for example F5 to save and F7 to
restore.

| Key | Action |
|---|---|
| F12 | HD or original mode |
| F11 or Alt+Enter | Fullscreen |

The scale N is always a whole number. The game is 320 x 200 original
pixels, shown at 320N x 200N screen pixels, with black bars around it
where the window is larger.

## Saves

The game's own save and restore dialogs work as in the original. Each
save is one file, `saves/sierra-hd-001.sav` to `sierra-hd-099.sav`, next
to `sierra-hd.ini`. A save holds the game's identity: it restores only with
the same release of the game. Files the game itself writes go to `files/`.

## Music: AdLib or MT-32

The music plays on an emulated AdLib by default. For the Roland MT-32:

1. Put the ROM files in a folder: a control ROM and a PCM ROM, of the
   MT-32 or the CM-32L, with names ending in `.rom` (any names; the
   program recognises the contents).
2. In `sierra-hd.ini`, set `mt32_rom` to that folder and `music = mt32`.

If the ROMs are missing or not recognised, the program says so and plays
on the AdLib.

## Configuration

`sierra-hd.ini` next to the program is written when you quit. Delete it
to be asked for the game folder again. Its keys:

| Key | Values |
|---|---|
| `game_folder` | the game folder |
| `mode` | `hd` or `original` |
| `scale` | `auto` (the largest that fits) or 1 to 16 |
| `priority_snap` | `on`: characters pass behind the foreground in whole original pixels, as in the original game; `off` (default): along its smooth edges |
| `music` | `adlib`, `mt32` or `off` |
| `mt32_rom` | the folder of the MT-32 ROMs |
| `volume` | 0 to 100 |
| `saves` | the folder of the saves (empty: `saves/` next to the file) |

The `hints` folder holds the scene-analysis hints for the HD mode. Keep it
next to the program; without it, HD pictures are drawn without them.

## The room viewer

    sierra-hd --viewer "C:\GOG Games\Colonel's Bequest"

shows every background picture, every animated view (sprite) and every
font, in HD or in the original look.

| Key | Action |
|---|---|
| Left, Right | Previous or next picture, view or font |
| Up, Down | Views: previous or next animation loop |
| Tab | Switch between pictures, views and fonts |
| H | HD or original mode |
| P | Pictures: show the priority map (the depth information) |
| + and - | Larger or smaller scale; 0: the largest that fits |
| Space | Views: stop or start the animation |
| F11 or Alt+Enter | Fullscreen |
| Escape | Quit |

`sierra-hd --help` lists the keys and options.

## Licence

sierra-hd is free software under the GNU General Public License, version 3
or later; see `licenses/`. It includes SDL 3 (zlib licence), Nuked OPL3
(LGPL 2.1 or later) and Munt mt32emu (LGPL 2.1 or later). *The Colonel's
Bequest* is a trademark of its owners; sierra-hd is not affiliated with
them.
