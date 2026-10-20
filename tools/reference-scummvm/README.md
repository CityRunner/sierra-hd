# Reference ScummVM

The behavioural reference of the project (spec section 3.3): ScummVM built
from the pinned revision in `REVISION` with the SCI engine only, and without
scalers, aspect-ratio correction or undithering. It is development tooling
only and is never linked into the program.

Build it inside the toolchain container (the ScummVM checkout stays outside
the repository and is mounted read-only):

    git clone https://github.com/scummvm/scummvm /path/to/scummvm
    git -C /path/to/scummvm checkout "$(cat tools/reference-scummvm/REVISION)"
    SIERRA_HD_MOUNTS=/path/to/scummvm:/scummvm \
        docker/run.sh tools/reference-scummvm/build.sh /scummvm build/reference-scummvm

Run it, headless if needed:

    docker/run.sh xvfb-run -a env SDL_AUDIODRIVER=dummy \
        tools/reference-scummvm/run.sh build/reference-scummvm --version

With the game folder mounted (for example at `/game`), add its path as the
second argument; ScummVM then detects and starts the game.

Patches for buffer dumps, kernel-call traces and the deterministic mode go to
`tools/scummvm-patches/` as `NNNN-description.patch` files and are applied in
name order by `build.sh`.
