#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares the MIDI messages of a reference run with those of a sci-vm run.

    compare-midi.py <reference file> <sci-vm file>

Both files hold one line per message the SCI MT-32 driver sends
(docs/formats/replay-and-trace.md, "MIDI files"):

    <timer call> <byte> <byte> ...

the reference's from SIERRA_HD_MIDI, ours from sci-vm-trace --midi. Prints
the first difference (line, timer call and bytes) and exits with 1, or
prints "midi match: <n> messages (...)" with the timer calls they span, the
note-ons and the SysEx messages among them.
"""

import pathlib
import sys


def describe(line: str) -> str:
    return line if len(line) < 200 else line[:197] + "..."


def counts(lines: list[str]) -> tuple[int, int]:
    """Note-ons with a velocity, and SysEx messages (longer than three bytes)."""
    notes = sysex = 0
    for line in lines:
        parts = line.split()[1:]
        if len(parts) > 3:
            sysex += 1
        elif len(parts) == 3 and parts[0].startswith("9") and parts[2] != "00":
            notes += 1
    return notes, sysex


def summary(lines: list[str]) -> str:
    if not lines:
        return "none"
    first, last = lines[0].split(" ", 1)[0], lines[-1].split(" ", 1)[0]
    notes, sysex = counts(lines)
    return f"timer calls {first} to {last}, {notes} note-ons, {sysex} SysEx"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    reference = pathlib.Path(sys.argv[1]).read_text(errors="replace").splitlines()
    ours = pathlib.Path(sys.argv[2]).read_text(errors="replace").splitlines()
    for number, (ref_line, line) in enumerate(zip(reference, ours), start=1):
        if ref_line == line:
            continue
        ref_call, our_call = ref_line.split(" ", 1)[0], line.split(" ", 1)[0]
        what = "timer call" if ref_call != our_call else "bytes"
        print(
            f"first difference at line {number}, timer call {ref_call}: {what} differ\n"
            f"  reference: {describe(ref_line)}\n"
            f"  sci-vm:    {describe(line)}"
        )
        return 1
    if len(reference) != len(ours):
        longer, side = (reference, "the reference") if len(reference) > len(ours) else (ours, "sci-vm")
        print(
            f"{len(reference)} messages in the reference, {len(ours)} in sci-vm; only {side} has "
            f"{describe(longer[min(len(reference), len(ours))])}"
        )
        return 1
    print(f"midi match: {len(reference)} messages ({summary(reference)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
