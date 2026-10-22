#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares the screens and maps of a reference run with those of a sci-vm run.

    compare-screens.py <reference folder> <sci-vm folder> [--all]

Both folders hold <tick>.ppm files (the screen the player sees) and
<tick>.priority.pgm and <tick>.control.pgm files (the 1x maps the logic
reads): the reference's from SIERRA_HD_SCREENS, ours from sci-vm-trace
--screens. The files of the ticks both runs wrote are compared pixel by
pixel. Prints the first difference of the screens and of the maps (or, with
--all, every one) and exits with 1 if any differs or a file is missing on
one side.
"""

import pathlib
import sys


def read_image(path: pathlib.Path) -> tuple[int, int, int, bytes]:
    """Width, height, bytes per pixel and pixels of a binary PPM or PGM."""
    data = path.read_bytes()
    magic, size, _depth, pixels = data.split(b"\n", 3)
    if magic not in (b"P6", b"P5"):
        raise ValueError(f"{path}: not a binary PPM or PGM")
    width, height = (int(value) for value in size.split())
    return width, height, 3 if magic == b"P6" else 1, pixels


def difference(reference: pathlib.Path, ours: pathlib.Path) -> str | None:
    ref_width, ref_height, ref_step, ref_pixels = read_image(reference)
    width, height, step, pixels = read_image(ours)
    if (ref_width, ref_height, ref_step) != (width, height, step):
        return f"size {ref_width}x{ref_height} vs {width}x{height}"
    if ref_pixels == pixels:
        return None
    differing = [
        index // step
        for index in range(0, len(pixels), step)
        if pixels[index : index + step] != ref_pixels[index : index + step]
    ]
    if not differing:
        return None
    xs = [index % width for index in differing]
    ys = [index // width for index in differing]
    return f"{len(differing)} pixels differ in x {min(xs)}..{max(xs)}, y {min(ys)}..{max(ys)}"


def compare(reference: pathlib.Path, ours: pathlib.Path, patterns: tuple[str, ...], what: str, show_all: bool) -> bool:
    """Compares the files matching `patterns`; prints a summary line for `what`."""
    ref_ticks = {path.name for pattern in patterns for path in reference.glob(pattern)}
    our_ticks = {path.name for pattern in patterns for path in ours.glob(pattern)}
    failed = False
    for name in sorted(ref_ticks ^ our_ticks):
        side = "sci-vm" if name in ref_ticks else "the reference"
        print(f"{name}: missing in {side}")
        failed = True
        if not show_all:
            break
    compared = 0
    differing = 0
    for name in sorted(ref_ticks & our_ticks):
        compared += 1
        message = difference(reference / name, ours / name)
        if message is None:
            continue
        differing += 1
        if show_all or differing == 1:
            print(f"{name}: {message}")
    if differing:
        failed = True
    # A frame is a tick: one screen, or a priority and a control map.
    frames = len({name.split(".")[0] for name in ref_ticks & our_ticks})
    print(f"{compared} {what} compared, {differing} differ" if failed else f"{what} match: {frames}")
    return not failed


def main() -> int:
    args = [arg for arg in sys.argv[1:] if arg != "--all"]
    if len(args) != 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    show_all = "--all" in sys.argv[1:]
    reference, ours = (pathlib.Path(arg) for arg in args)
    screens = compare(reference, ours, ("*.ppm",), "screens", show_all)
    maps = compare(reference, ours, ("*.priority.pgm", "*.control.pgm"), "maps", show_all)
    return 0 if screens and maps else 1


if __name__ == "__main__":
    sys.exit(main())
