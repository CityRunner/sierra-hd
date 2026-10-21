# sierra-hd 0.1

sierra-hd is a new interpreter for Sierra's *The Colonel's Bequest*
(1989), with an HD mode that redraws the backgrounds as sharp vector art
at any whole-number scale.

**Release 0.1 is a room viewer, not yet the game.** It shows every
background picture, every animated view (sprite) and every font of your
copy of the game, in HD or in the original look. The interpreter that plays
the game follows in later releases.

## What you need

- Your own copy of *The Colonel's Bequest*, English DOS version (the GOG
  release works). sierra-hd contains no game data.
- One of:
  - Windows 10 or 11 (x64 or ARM64);
  - Linux with glibc 2.38 or newer, for example Ubuntu 24.04, Debian 13 or
    Fedora 39, on x64 or ARM64, with X11 or Wayland.

## Starting

Unpack the archive anywhere and start `sierra-hd` (on Windows
`sierra-hd.exe`). The first time, it asks for the game folder: the one that
contains `RESOURCE.MAP`. You can also give the folder on the command line:

    sierra-hd "C:\GOG Games\Colonel's Bequest"

The program checks that the folder holds a supported release. The tested
release is the English DOS 3.5" floppy version 1.000.046. The 5.25"
version is accepted with a warning, and its pictures are drawn without the
HD hints.

## Keys

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

The scale N is always a whole number. The picture is 320 x 200 original
pixels, shown at 320N x 200N screen pixels, with black bars around it
where the window is larger.

## Configuration

`sierra-hd.ini` next to the program keeps the game folder, the mode and
the scale. It is written when you quit. Delete it to be asked for the game
folder again.

The `hints` folder holds the scene-analysis hints for the HD mode. Keep it
next to the program; without it, HD pictures are drawn without them.

## Licence

sierra-hd is free software under the GNU General Public License, version 3
or later; see `licenses/`. It includes SDL 3 (zlib licence). *The Colonel's
Bequest* is a trademark of its owners; sierra-hd is not affiliated with
them.
