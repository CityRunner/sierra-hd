#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares the window layout of a reference run with that of a sci-vm run.

    compare-layout.py <reference file> <sci-vm file>

Both files hold one line per window, text, control, menu and cursor
operation in 1x screen coordinates (docs/formats/replay-and-trace.md,
"Layout files"):

    <tick> <call> <kind> <fields>

the reference's from SIERRA_HD_LAYOUT, ours from sci-vm-trace --layout.
Prints the first difference (tick, call, kind and the fields that differ)
and exits with 1, or prints "layout match: <n> lines (...)" with the count
of each kind.
"""

import collections
import pathlib
import re
import sys

# A field: name=value, the value quoted or up to the next space.
FIELD = re.compile(r'(\w+)=("(?:[^"\\]|\\.)*"|\S+)')

# The kinds counted in the summary, in this order.
KINDS = [
    "window", "dispose", "picwindow", "text", "box", "size", "display", "control", "invert",
    "menubar", "menu", "savebox", "restorebox", "cursor",
]


def fields(line: str) -> dict[str, str]:
    """The fields of a line by name, for describing a difference: tick, call,
    kind, the named fields and the words between them in order."""
    tick, call, kind, rest = (line.split(" ", 3) + ["", "", ""])[:4]
    result = {"tick": tick, "call": call, "kind": kind}
    for number, word in enumerate(re.findall(r'\w+="(?:[^"\\]|\\.)*"|"(?:[^"\\]|\\.)*"|\S+', rest)):
        named = FIELD.fullmatch(word)
        if named:
            result[named.group(1)] = named.group(2)
        else:
            result[f"#{number}"] = word
    return result


def describe(line: str) -> str:
    return line if len(line) < 200 else line[:197] + "..."


def summary(lines: list[str]) -> str:
    kinds = collections.Counter(line.split(" ", 3)[2] for line in lines if line.count(" ") >= 2)
    parts = [f"{kinds[kind]} {kind}" for kind in KINDS if kinds[kind]]
    parts += [f"{count} {kind}" for kind, count in sorted(kinds.items()) if kind not in KINDS]
    return ", ".join(parts)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    reference = pathlib.Path(sys.argv[1]).read_text(errors="replace").splitlines()
    ours = pathlib.Path(sys.argv[2]).read_text(errors="replace").splitlines()
    for ref_line, line in zip(reference, ours):
        if ref_line == line:
            continue
        ref_fields, our_fields = fields(ref_line), fields(line)
        names = [name for name in ref_fields if ref_fields.get(name) != our_fields.get(name)]
        names += [name for name in our_fields if name not in ref_fields]
        print(
            f"first difference at tick {ref_fields['tick']}, call {ref_fields['call']} "
            f"({ref_fields['kind']}): {', '.join(names)} differ\n"
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
    print(f"layout match: {len(reference)} lines ({summary(reference) or 'none'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
