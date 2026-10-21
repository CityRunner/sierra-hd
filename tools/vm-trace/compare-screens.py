#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares the screens of a reference run with those of a sci-vm run.

    compare-screens.py <reference folder> <sci-vm folder> [--all]

Both folders hold <tick>.ppm files: the reference's from
SIERRA_HD_SCREENS, ours from sci-vm-trace --screens. Screens of the ticks
both runs wrote are compared pixel by pixel. Prints the first difference
(or, with --all, every one) and exits with 1 if any screen differs or a
tick is missing on one side.
"""

import pathlib
import sys


def read_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, size, depth, pixels = data.split(b"\n", 3)
    if magic != b"P6" or depth != b"255":
        raise ValueError(f"{path}: not an 8-bit binary PPM")
    width, height = (int(value) for value in size.split())
    return width, height, pixels


def difference(reference: pathlib.Path, ours: pathlib.Path) -> str | None:
    ref_width, ref_height, ref_pixels = read_ppm(reference)
    width, height, pixels = read_ppm(ours)
    if (ref_width, ref_height) != (width, height):
        return f"size {ref_width}x{ref_height} vs {width}x{height}"
    differing = [
        index // 3 for index in range(0, len(pixels), 3) if pixels[index : index + 3] != ref_pixels[index : index + 3]
    ]
    if not differing:
        return None
    xs = [index % width for index in differing]
    ys = [index // width for index in differing]
    return f"{len(differing)} pixels differ in x {min(xs)}..{max(xs)}, y {min(ys)}..{max(ys)}"


def main() -> int:
    args = [arg for arg in sys.argv[1:] if arg != "--all"]
    if len(args) != 2:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    show_all = "--all" in sys.argv[1:]
    reference, ours = (pathlib.Path(arg) for arg in args)
    ref_ticks = {path.name for path in reference.glob("*.ppm")}
    our_ticks = {path.name for path in ours.glob("*.ppm")}
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
    print(f"{compared} screens compared, {differing} differ" if failed else f"screens match: {compared}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
