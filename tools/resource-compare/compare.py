#!/usr/bin/env python3
"""Compares two resource dumps (manifest.txt plus one file per resource).

    compare.py <reference dump> <sierra-hd dump>

Both dumps use the layout of tools/scummvm-patches/0001-dump-resources.patch.
Exits 0 if the manifests are identical and every data file matches byte for
byte; otherwise lists the differences (at most 50 of each kind) and exits 1.
"""

import sys
from pathlib import Path

LIMIT = 50


def read_manifest(dump: Path) -> list[str]:
    return (dump / "manifest.txt").read_text(encoding="ascii").splitlines()


def data_name(line: str) -> str | None:
    fields = line.split()
    if len(fields) < 3 or fields[2] == "FAILED":
        return None
    return f"{fields[0]}.{int(fields[1]):03d}"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    reference, candidate = Path(sys.argv[1]), Path(sys.argv[2])
    ref_lines, cand_lines = read_manifest(reference), read_manifest(candidate)

    problems = 0
    ref_set, cand_set = set(ref_lines), set(cand_lines)
    for label, missing in (("only in the reference", ref_set - cand_set), ("only in sierra-hd", cand_set - ref_set)):
        for line in sorted(missing)[:LIMIT]:
            print(f"manifest line {label}: {line}")
        problems += len(missing)

    compared = 0
    differing = []
    for line in sorted(ref_set & cand_set):
        name = data_name(line)
        if name is None:
            continue
        compared += 1
        if (reference / name).read_bytes() != (candidate / name).read_bytes():
            differing.append(name)
    for name in differing[:LIMIT]:
        print(f"data differs: {name}")
    problems += len(differing)

    failed = sum(1 for line in ref_lines if data_name(line) is None)
    print(
        f"{len(ref_lines)} resources in the reference, {len(cand_lines)} in sierra-hd; "
        f"{compared} compared byte for byte, {len(differing)} differ; "
        f"{failed} failed in the reference; {problems} problems"
    )
    return 0 if problems == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
