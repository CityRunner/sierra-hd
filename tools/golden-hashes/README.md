# golden-hashes

Golden hashes of every picture's background (spec section 5.3): SHA-256 of
the HD frame (picture area, RGBA) at N = 1, 2, 5 and 10 and of the original
frame at N = 1, for every picture alone and for every picture stack the
game's scripts draw (a picture with overlays drawn over it by kDrawPic with
add-to). They catch changes, not quality. The committed list is
`tests/golden/pictures.txt` (tested release), made with the committed hint
files (`--hints hints`; spec section 6.7 asks for golden hashes of a fixed,
versioned set of hints).

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        build/release/tools/golden-hashes/sci-golden-hashes /game tests/golden/pictures.txt --check --hints hints

One line per picture and kind (`hd1`, `hd2`, `hd5`, `hd10`, `original1`):

    pic <n> <kind> <hash>

then one line per stack and kind:

    stack <stack> <kind> <hash>

`<stack>` is the base picture, then each overlay in drawing order, joined
by `+`; a picture drawn with kDrawPic's options has `.mirrored` and
`.palette<n>` after its number, as the analysis cache names its entries:
`stack 174.palette1+74 hd5 ...` is picture 174 with EGA palette 1, then 74
over it with palette 0. The HD frames of a stack come from the analysis
cache's variant of the stack (with `--cache`, from that cache), as the
program draws them; the original frame is the palette-index frame of the
stack drawn with the same options. The stacks and where the scripts draw
them are listed in `docs/ASSUMPTIONS.md` ("Milestone 5: the picture
stacks").

Without `--check` it writes the file. On ARM64 under qemu-user:

    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh \
        qemu-aarch64 build/cross-aarch64/tools/golden-hashes/sci-golden-hashes /game tests/golden/pictures.txt --check --hints hints

Regenerate the file only for an intended change of the output, and say why
in the commit.
