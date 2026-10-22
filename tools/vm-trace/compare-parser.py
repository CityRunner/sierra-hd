#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares the parser results of a reference run with those of a sci-vm run.

    compare-parser.py <reference file> <sci-vm file>

Both files hold one line per kParse and kSaid call (docs/formats/replay-and-trace.md):

    <tick> <call> parse "<input>" <outcome> [word=...] [words=...] [tree=...] type=<n> claimed=<n>
    <tick> <call> said <spec> <outcome> claimed=<n>

the reference's from SIERRA_HD_PARSER, ours from sci-vm-trace --parser.
Prints the first difference (tick, call, input or spec, and the field that
differs) and exits with 1, or prints "parser match: <n> parses, <m> said
calls (<full> full, <partial> partial matches)".
"""

import pathlib
import re
import sys

# A parse line after its tick and call: the quoted input, then the fields.
PARSE = re.compile(r'parse ("(?:[^"\\]|\\.)*") (\S+)(.*)$')


def fields(line: str) -> dict[str, str]:
    """The fields of a line by name, for describing a difference."""
    tick, call, kind, rest = line.split(" ", 3)
    result = {"tick": tick, "call": call, "kind": kind}
    if kind == "parse":
        match = PARSE.match(f"parse {rest}")
        if not match:
            return {**result, "line": rest}
        result["input"] = match.group(1)
        result["outcome"] = match.group(2)
        for item in re.findall(r'(\w+)=("(?:[^"\\]|\\.)*"|\S+)', match.group(3)):
            result[item[0]] = item[1]
    else:
        spec, outcome, claimed = (rest.split(" ") + ["", ""])[:3]
        result.update(spec=spec, outcome=outcome, claimed=claimed.removeprefix("claimed="))
    return result


def describe(line: str) -> str:
    return line if len(line) < 200 else line[:197] + "..."


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    reference = pathlib.Path(sys.argv[1]).read_text().splitlines()
    ours = pathlib.Path(sys.argv[2]).read_text().splitlines()
    for ref_line, line in zip(reference, ours):
        if ref_line == line:
            continue
        ref_fields, our_fields = fields(ref_line), fields(line)
        what = ref_fields.get("input", ref_fields.get("spec", ""))
        names = [name for name in ref_fields if ref_fields.get(name) != our_fields.get(name)]
        names += [name for name in our_fields if name not in ref_fields]
        print(
            f"first difference at tick {ref_fields['tick']}, call {ref_fields['call']} "
            f"({ref_fields['kind']} {what}): {', '.join(names)} differ\n"
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
    parses = sum(1 for line in reference if line.split(" ", 3)[2] == "parse")
    saids = [fields(line)["outcome"] for line in reference if line.split(" ", 3)[2] == "said"]
    print(
        f"parser match: {parses} parses, {len(saids)} said calls "
        f"({saids.count('full')} full, {saids.count('partial')} partial matches)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
