#!/usr/bin/env python3
"""Repository policy checks that are not covered by clang-tidy.

1. No game resources, ROM files or images derived from the game art are
   tracked (spec sections 2 and 3.4). Images are allowed only where listed
   in ALLOWED_IMAGE_DIRS (none so far).
2. No float or long double in project code (spec section 5.3: double only).
3. Every project source file starts with an SPDX licence identifier.

Run from anywhere; exits non-zero and lists every violation.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

GAME_DATA = re.compile(
    r"(^|/)(resource\.(map|\d{3})|message\.(map|\d{3})|[^/]+\.(scr|hep|drv|p56|v56|sci|rom))$",
    re.IGNORECASE,
)
IMAGE = re.compile(r"\.(png|ppm|pgm|bmp|gif|jpe?g|tga|webp)$", re.IGNORECASE)
ALLOWED_IMAGE_DIRS: tuple[str, ...] = ()
SOURCE = re.compile(r"\.(cpp|hpp)$")
FLOAT_TYPES = re.compile(r"\b(float|long\s+double)\b")
SPDX = "SPDX-License-Identifier:"


def tracked_files() -> list[str]:
    """Tracked and new (not ignored) files that exist in the working tree."""
    out = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    ).stdout
    return sorted({p for p in out.decode().split("\0") if p and (ROOT / p).is_file()})


def strip_comments_and_strings(text: str) -> str:
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r'"(\\.|[^"\\])*"', '""', text)


def main() -> int:
    problems: list[str] = []
    for path in tracked_files():
        if path.startswith("third_party/"):
            continue
        if GAME_DATA.search(path):
            problems.append(f"{path}: looks like a game resource or ROM file")
        if IMAGE.search(path) and not path.startswith(ALLOWED_IMAGE_DIRS or ("\0",)):
            problems.append(f"{path}: images are not committed (reports/images/ is ignored)")
        if SOURCE.search(path):
            text = (ROOT / path).read_text(encoding="utf-8")
            if SPDX not in "\n".join(text.splitlines()[:3]):
                problems.append(f"{path}: missing SPDX licence identifier in the first lines")
            for number, line in enumerate(strip_comments_and_strings(text).splitlines(), 1):
                if FLOAT_TYPES.search(line):
                    problems.append(f"{path}:{number}: float and long double are not used (spec 5.3)")

    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
