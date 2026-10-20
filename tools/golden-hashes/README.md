# golden-hashes

Golden hashes of every picture's background (spec section 5.3): SHA-256 of
the HD frame (picture area, RGBA) at N = 1, 2, 5 and 10 and of the original
frame at N = 1. They catch changes, not quality. The committed list is
`tests/golden/pictures.txt` (tested release).

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/release/tools/golden-hashes/sci-golden-hashes /game tests/golden/pictures.txt --check

Without `--check` it writes the file. On ARM64 under qemu-user:

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        qemu-aarch64 build/cross-aarch64/tools/golden-hashes/sci-golden-hashes /game tests/golden/pictures.txt --check

Regenerate the file only for an intended change of the output, and say why
in the commit.
