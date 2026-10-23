#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares the OPL register writes of a reference run with those of a sci-vm run.

    compare-opl.py <reference file> <sci-vm file>

Both files hold one line per register write of the AdLib driver
(docs/formats/replay-and-trace.md, "OPL files"):

    <timer call> <register> <value>

the reference's from SIERRA_HD_OPL, ours from sci-vm-trace --opl. Prints
the first difference (line, timer call, register and value) and exits with
1, or prints "opl match: <n> writes (...)" with the timer calls they span
and the key-ons among them.
"""

import pathlib
import sys


def describe(line: str) -> str:
    return line if len(line) < 200 else line[:197] + "..."


def key_ons(lines: list[str]) -> int:
    """Writes to 0xB0-0xB8 (either bank) with the key-on bit set."""
    count = 0
    for line in lines:
        parts = line.split()
        if len(parts) != 3:
            continue
        try:
            reg, value = int(parts[1], 16), int(parts[2], 16)
        except ValueError:
            continue
        if 0xB0 <= (reg & 0xFF) <= 0xB8 and value & 0x20:
            count += 1
    return count


def summary(lines: list[str]) -> str:
    if not lines:
        return "none"
    first, last = lines[0].split(" ", 1)[0], lines[-1].split(" ", 1)[0]
    return f"timer calls {first} to {last}, {key_ons(lines)} key-ons"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    reference = pathlib.Path(sys.argv[1]).read_text(errors="replace").splitlines()
    ours = pathlib.Path(sys.argv[2]).read_text(errors="replace").splitlines()
    for number, (ref_line, line) in enumerate(zip(reference, ours), start=1):
        if ref_line == line:
            continue
        ref_parts = (ref_line.split() + ["", "", ""])[:3]
        our_parts = (line.split() + ["", "", ""])[:3]
        names = [name for name, a, b in zip(("timer call", "register", "value"), ref_parts, our_parts) if a != b]
        print(
            f"first difference at line {number}, timer call {ref_parts[0]}: {', '.join(names)} differ\n"
            f"  reference: {describe(ref_line)}\n"
            f"  sci-vm:    {describe(line)}"
        )
        return 1
    if len(reference) != len(ours):
        longer, side = (reference, "the reference") if len(reference) > len(ours) else (ours, "sci-vm")
        print(
            f"{len(reference)} lines in the reference, {len(ours)} in sci-vm; only {side} has "
            f"{describe(longer[min(len(reference), len(ours))])}"
        )
        return 1
    print(f"opl match: {len(reference)} writes ({summary(reference)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
