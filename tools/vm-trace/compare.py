#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compares two kernel-call traces (docs/formats/replay-and-trace.md).

  compare.py <reference trace> <sci-vm trace> [--context N]

Prints the first line that differs with the lines before it, and exits 0 if
the sci-vm trace equals the reference (or is a prefix of it when the
reference ran further, with --prefix).
"""

import argparse
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference")
    parser.add_argument("actual")
    parser.add_argument("--context", type=int, default=5)
    parser.add_argument("--prefix", action="store_true", help="accept a sci-vm trace that stops early")
    args = parser.parse_args()
    with open(args.reference, encoding="ascii") as file:
        reference = file.read().splitlines()
    with open(args.actual, encoding="ascii") as file:
        actual = file.read().splitlines()
    for index, (want, got) in enumerate(zip(reference, actual)):
        if want != got:
            for line in reference[max(0, index - args.context):index]:
                print(f"  {line}")
            print(f"- {want}")
            print(f"+ {got}")
            print(f"first difference at call {index} of {len(reference)}")
            return 1
    if len(actual) < len(reference):
        print(f"sci-vm trace stops after {len(actual)} of {len(reference)} calls; next: {reference[len(actual)]}")
        return 0 if args.prefix else 1
    if len(actual) > len(reference):
        print(f"sci-vm trace has {len(actual) - len(reference)} more calls than the reference")
        return 1
    print(f"traces match: {len(reference)} calls")
    return 0


if __name__ == "__main__":
    sys.exit(main())
