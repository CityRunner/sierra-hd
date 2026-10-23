# sound-render

Plays sound resources of the game through sci-vm's music player and
sci-audio's AdLib driver (`sci::vm::render_sound`), and prints for each the
OPL output at the chip's native rate (49716 Hz): frames, music timer calls,
whether the song ended (`ended`) or was cut at the limit (`cut`), the peak
sample and the SHA-256 of the samples (two bytes little-endian each, left
then right).

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/debug/tools/sound-render/sci-sound-render /game <sound number | all> [seconds] [--raw <file>]

`<seconds>` (default 60) limits each sound; looping songs play once, at
the game's start master volume (11). `--raw` writes one sound's samples as
signed 16-bit little-endian stereo, for listening:

    sox -t raw -r 49716 -e signed -b 16 -c 2 sound.raw sound.wav

The output is a pure function of the game files, the same on every
platform: the basis of the OPL golden hashes (milestone 7).
