# sound-render

Plays sound resources of the game through sci-vm's music player and
sci-audio's AdLib driver (`sci::vm::render_sound`), and prints for each the
OPL output at the chip's native rate (49716 Hz): frames, music timer calls,
whether the song ended (`ended`) or was cut at the limit (`cut`), the peak
sample and the SHA-256 of the samples (two bytes little-endian each, left
then right).

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/debug/tools/sound-render/sci-sound-render /game <sound number | all> [seconds] [--raw <file>] [--opl <file>]

`<seconds>` (default 60) limits each sound; looping songs play once, at
the game's start master volume (11). `--raw` writes one sound's samples as
signed 16-bit little-endian stereo, for listening:

    sox -t raw -r 49716 -e signed -b 16 -c 2 sound.raw sound.wav

`--opl` writes one sound's OPL register writes, keyed by the music timer
call, as `sci-vm-trace --opl` does (`docs/formats/replay-and-trace.md`,
"OPL files").

The output is a pure function of the game files, the same on every
platform: the basis of the OPL golden hashes (milestone 7). They are kept
in `tests/golden/sounds.txt`, one line per sound resource (all 119 of the
game), each rendered for its first 20 seconds or to the song's end (84 end
sooner):

    sound <n> adlib20 <SHA-256 of the samples>

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/release/tools/sound-render/sci-sound-render /game --golden tests/golden/sounds.txt --check

prints each difference and `all 119 sound golden hashes match` (about 35 s
in release, 3 minutes on ARM64 under qemu-user):

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        qemu-aarch64 build/cross-aarch64/tools/sound-render/sci-sound-render /game --golden tests/golden/sounds.txt --check

Without `--check` it writes the file. Regenerate it only for an intended
change of the output, and say why in the commit.
