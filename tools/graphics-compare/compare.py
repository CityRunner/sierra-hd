#!/usr/bin/env python3
"""Compares two graphics dumps (pictures, views and fonts).

    compare.py <reference dump> <sierra-hd dump>

Both dumps use the layout of tools/scummvm-patches/0002-dump-graphics.patch.
Exits 0 if the manifests are identical and every file matches byte for byte.
Otherwise it lists the differences (at most 50) and exits 1. For pictures it
gives the number of differing pixels in the visual, priority and control
maps.
"""

import sys
from collections import Counter
from pathlib import Path

LIMIT = 50
MAP_SIZE = 320 * 200
MAPS = ("visual", "priority", "control")


def describe(name: str, reference: bytes, candidate: bytes) -> str:
    if name.startswith("pic.") and len(reference) == len(candidate) == 3 * MAP_SIZE:
        counts = []
        for index, label in enumerate(MAPS):
            start = index * MAP_SIZE
            differing = sum(
                1 for a, b in zip(reference[start : start + MAP_SIZE], candidate[start : start + MAP_SIZE]) if a != b
            )
            counts.append(f"{label} {differing}")
        return f"{name}: pixels differ ({', '.join(counts)})"
    if len(reference) != len(candidate):
        return f"{name}: {len(reference)} bytes in the reference, {len(candidate)} in sierra-hd"
    first = next(i for i, (a, b) in enumerate(zip(reference, candidate)) if a != b)
    return f"{name}: first difference at byte {first}"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    reference, candidate = Path(sys.argv[1]), Path(sys.argv[2])
    ref_lines = (reference / "manifest.txt").read_text(encoding="ascii").splitlines()
    cand_lines = (candidate / "manifest.txt").read_text(encoding="ascii").splitlines()

    problems = []
    for line in sorted(set(ref_lines) ^ set(cand_lines)):
        side = "reference" if line in ref_lines else "sierra-hd"
        problems.append(f"manifest line only in {side}: {line}")

    compared = Counter()
    differing = Counter()
    for line in sorted(set(ref_lines) & set(cand_lines)):
        name, size = line.split()
        if size == "FAILED":
            continue
        kind = name.split(".")[0] if not name.startswith("pic.") else "pic." + name.split(".")[2]
        compared[kind] += 1
        ref_bytes = (reference / name).read_bytes()
        cand_bytes = (candidate / name).read_bytes()
        if ref_bytes != cand_bytes:
            differing[kind] += 1
            problems.append(describe(name, ref_bytes, cand_bytes))

    for problem in problems[:LIMIT]:
        print(problem)
    for kind in sorted(compared):
        print(f"{kind}: {compared[kind] - differing[kind]} of {compared[kind]} identical")
    print(f"{sum(compared.values())} files compared, {len(problems)} problems")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
