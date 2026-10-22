#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares the actor positions of a reference run with those of a sci-vm run.

    compare-actors.py <reference file> <sci-vm file>

Both files hold the cast of every kAnimate call, one line per entry in
drawing order (docs/formats/replay-and-trace.md):

    <tick> <call> <object> <x> <y> <z> <priority> <view> <loop> <cel> <signal>

the reference's from SIERRA_HD_ACTORS, ours from sci-vm-trace --actors.
Prints the first difference (tick, call, object and field) and exits with 1,
or prints "actors match: <frames> frames", a frame being a kAnimate call
that drew a cast.
"""

import pathlib
import sys

FIELDS = ("tick", "call", "object", "x", "y", "z", "priority", "view", "loop", "cel", "signal")


def read_frames(path: pathlib.Path) -> list[tuple[str, list[list[str]]]]:
    """The frames in file order: (call number, its entries)."""
    frames: list[tuple[str, list[list[str]]]] = []
    for number, line in enumerate(path.read_text().splitlines(), start=1):
        fields = line.split()
        if len(fields) != len(FIELDS):
            raise ValueError(f"{path}:{number}: expected {len(FIELDS)} fields: {line}")
        if not frames or frames[-1][0] != fields[1]:
            frames.append((fields[1], []))
        frames[-1][1].append(fields)
    return frames


def describe(entry: list[str]) -> str:
    return " ".join(f"{name}={value}" for name, value in zip(FIELDS[3:], entry[3:]))


def compare_frame(reference: list[list[str]], ours: list[list[str]]) -> str | None:
    for ref_entry, our_entry in zip(reference, ours):
        if ref_entry == our_entry:
            continue
        where = f"tick {ref_entry[0]}, call {ref_entry[1]}, object {ref_entry[2]}"
        for name, ref_value, value in zip(FIELDS, ref_entry, our_entry):
            if ref_value != value:
                return (
                    f"first difference at {where}: {name} is {ref_value} in the reference, {value} in sci-vm\n"
                    f"  reference: {describe(ref_entry)}\n"
                    f"  sci-vm:    {describe(our_entry)}"
                )
    if len(reference) != len(ours):
        longer, side = (reference, "the reference") if len(reference) > len(ours) else (ours, "sci-vm")
        extra = longer[min(len(reference), len(ours))]
        return (
            f"first difference at tick {extra[0]}, call {extra[1]}: the cast has {len(reference)} entries in "
            f"the reference, {len(ours)} in sci-vm; only {side} has object {extra[2]} ({describe(extra)})"
        )
    return None


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    reference = read_frames(pathlib.Path(sys.argv[1]))
    ours = read_frames(pathlib.Path(sys.argv[2]))
    for (ref_call, ref_entries), (call, entries) in zip(reference, ours):
        if ref_call != call:
            print(
                f"first difference at frame {ref_call} (tick {ref_entries[0][0]}) in the reference, "
                f"frame {call} (tick {entries[0][0]}) in sci-vm: the kAnimate calls differ"
            )
            return 1
        message = compare_frame(ref_entries, entries)
        if message is not None:
            print(message)
            return 1
    if len(reference) != len(ours):
        longer, side = (reference, "the reference") if len(reference) > len(ours) else (ours, "sci-vm")
        call, entries = longer[min(len(reference), len(ours))]
        print(
            f"{len(reference)} frames in the reference, {len(ours)} in sci-vm; "
            f"only {side} has frame {call} (tick {entries[0][0]})"
        )
        return 1
    print(f"actors match: {len(reference)} frames")
    return 0


if __name__ == "__main__":
    sys.exit(main())
