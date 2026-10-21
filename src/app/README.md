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
- `src/`: the program `sierra-hd`. At release v0.1 (milestone 3a) it is the
  room viewer in an SDL3 window:
  - `viewer.cpp` composes the 320N x 200N frame (pictures in HD or original
    mode, views with their animation, fonts) and tracks the changed
    rectangles;
  - `main.cpp` holds the window, the keys and the presentation, uploading
    only the changed rectangles to the texture.

## Running

    sierra-hd [game folder]

Without a folder argument, the folder comes from `sierra-hd.ini` next to the
executable; on the first start, the program asks for it. The hint files are
read from `hints/` next to the executable. `sierra-hd --help` lists the keys.

## Invariants

- Libraries never reach the OS directly; app passes them data and services.
- Game files are opened read-only.
- N is an integer; the rest of the window is letterbox (spec section 6.1).
