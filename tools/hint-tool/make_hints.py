#!/usr/bin/env python3
"""Turns reviewed candidate decisions into hint files (hints/README.md).

Inputs, per picture NNN:
- <candidates>/pic.NNN.candidates.json, written by sci-hint-tool candidates;
- <candidates>/summary.txt, the tool's output lines ("pic N: ... sha256 H");
- <decisions>/pic.NNN.json, the reviewer's decisions:
  {"picture": N,
   "accepted": [{"candidate": "c22", "primitive": "arc", "label": "...", "uncertain": false}],
   "regions": [{"at": [x, y], "label": "...", "uncertain": false}],
   "symmetry": [{"kind": "mirror", "candidates": ["c22", "c23"], "note": "..."}],
   "notes": "..."}

Writes <hints>/pic.NNN.json for every picture with accepted chains or
regions. Ids are made from the labels. A decision that names an unknown
candidate, or a primitive the tool did not accept for it, is an error.

Usage: make_hints.py <candidates dir> <decisions dir> <hints dir> <analyser>
"""

import json
import re
import sys
from pathlib import Path

SUMMARY = re.compile(r"^pic (\d+): .* sha256 ([0-9a-f]{64})$")


def slug(label, used):
    base = re.sub(r"[^a-z0-9]+", "-", label.lower()).strip("-")[:28].strip("-") or "item"
    candidate, n = base, 2
    while candidate in used:
        candidate = f"{base}-{n}"
        n += 1
    used.add(candidate)
    return candidate


def text(value):
    value = str(value)
    if len(value) > 200 or not re.fullmatch(r"[ -~]*", value):
        raise ValueError(f"label {value!r} is not printable ASCII of at most 200 characters")
    return value


def make(number, sha, candidates, decisions, analyser):
    by_id = {c["id"]: c for c in candidates["candidates"]}
    used = set()
    chains, ids = [], {}
    for item in decisions.get("accepted", []):
        candidate = by_id.get(item["candidate"])
        if candidate is None:
            raise ValueError(f"pic {number}: unknown candidate {item['candidate']}")
        if item["primitive"] not in candidate["kinds"]:
            raise ValueError(f"pic {number}: {item['candidate']} cannot be a {item['primitive']}")
        hint_id = slug(item["label"], used)
        ids[item["candidate"]] = hint_id
        chain = {"id": hint_id, "lines": candidate["lines"], "primitive": item["primitive"], "label": text(item["label"])}
        if item.get("uncertain"):
            chain["uncertain"] = True
        chains.append(chain)
    regions = []
    for item in decisions.get("regions", []):
        x, y = item["at"]
        if not (0 <= x <= 319 and 0 <= y <= 189):
            raise ValueError(f"pic {number}: region {item['label']!r} lies outside the picture")
        region = {"id": slug(item["label"], used), "at": [x, y], "label": text(item["label"])}
        if item.get("uncertain"):
            region["uncertain"] = True
        regions.append(region)
    symmetry = []
    for item in decisions.get("symmetry", []):
        refs = [ids[c] for c in item["candidates"] if c in ids]
        if len(refs) >= 2 and len(set(refs)) == len(refs):
            entry = {"kind": item["kind"], "chains": refs}
            if item.get("note"):
                entry["note"] = text(item["note"])
            symmetry.append(entry)
    if not chains and not regions:
        return None
    hints = {
        "format": "sierra-hd-hints",
        "version": 1,
        "picture": number,
        "pic_sha256": sha,
        "analyser": analyser,
    }
    for key, value in (("regions", regions), ("chains", chains), ("symmetry", symmetry)):
        if value:
            hints[key] = value
    return hints


def dump(hints):
    # One item per line keeps reviews and diffs readable.
    lines = ["{"]
    scalars = [k for k in hints if not isinstance(hints[k], list)]
    lists = [k for k in hints if isinstance(hints[k], list)]
    for i, key in enumerate(scalars):
        comma = "," if i + 1 < len(scalars) or lists else ""
        lines.append(f"  {json.dumps(key)}: {json.dumps(hints[key])}{comma}")
    for i, key in enumerate(lists):
        lines.append(f"  {json.dumps(key)}: [")
        items = hints[key]
        for j, item in enumerate(items):
            lines.append(f"    {json.dumps(item)}{',' if j + 1 < len(items) else ''}")
        lines.append(f"  ]{',' if i + 1 < len(lists) else ''}")
    lines.append("}")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    candidates_dir, decisions_dir, hints_dir = (Path(a) for a in sys.argv[1:4])
    analyser = sys.argv[4]
    shas = {}
    for line in (candidates_dir / "summary.txt").read_text().splitlines():
        match = SUMMARY.match(line)
        if match:
            shas[int(match.group(1))] = match.group(2)
    written = 0
    for path in sorted(decisions_dir.glob("pic.*.json")):
        decisions = json.loads(path.read_text())
        number = int(decisions["picture"])
        candidates = json.loads((candidates_dir / f"pic.{number:03}.candidates.json").read_text())
        hints = make(number, shas[number], candidates, decisions, analyser)
        if hints is not None:
            (hints_dir / f"pic.{number:03}.json").write_text(dump(hints))
            written += 1
    print(f"{written} hint files written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
