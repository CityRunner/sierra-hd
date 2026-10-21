#!/usr/bin/env python3
"""Checks the hint files in hints/ (spec section 6.7) without the game.

Every hints/pic.NNN.json must satisfy hints/schema.json and the rules the
schema cannot state: the file name matches the picture, ids are unique,
no line is in two chains, a gap joins two different lines, and symmetry
refers to chains of the file, each once. The program's reader
(sci-vector, parse_hints) applies the same rules; whether the lines exist
in the picture is checked with the game files by sci-hint-tool check.

The schema is interpreted by a small validator for the keywords it uses,
so the check needs nothing beyond the Python standard library.

Usage: check-hints.py [directory]   (default: hints/ of the repository)
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FILE_NAME = re.compile(r"^pic\.(\d{3})\.json$")


def reject_duplicates(pairs):
    names = [name for name, _ in pairs]
    if len(names) != len(set(names)):
        raise ValueError("duplicate member")
    return dict(pairs)


def reject_floats(text):
    raise ValueError(f"only integers are allowed: {text}")


class Validator:
    def __init__(self, schema):
        self.schema = schema
        self.errors = []

    def resolve(self, node):
        while "$ref" in node:
            ref = node["$ref"]
            if not ref.startswith("#/$defs/"):
                raise ValueError(f"unsupported reference {ref}")
            node = self.schema["$defs"][ref[len("#/$defs/"):]]
        return node

    def fail(self, where, what):
        self.errors.append(f"{where}: {what}")

    def check(self, node, value, where):
        node = self.resolve(node)
        if "const" in node and value != node["const"]:
            self.fail(where, f"{node['const']!r} expected")
        if "enum" in node and value not in node["enum"]:
            self.fail(where, f"one of {node['enum']} expected")
        kind = node.get("type")
        checks = {
            "object": lambda v: isinstance(v, dict),
            "array": lambda v: isinstance(v, list),
            "string": lambda v: isinstance(v, str),
            "integer": lambda v: isinstance(v, int) and not isinstance(v, bool),
            "boolean": lambda v: isinstance(v, bool),
        }
        if kind is not None and not checks[kind](value):
            self.fail(where, f"{kind} expected")
            return
        if kind == "object":
            self.check_object(node, value, where)
        elif kind == "array":
            self.check_array(node, value, where)
        elif kind == "string":
            if len(value) > node.get("maxLength", len(value)):
                self.fail(where, "string too long")
            if "pattern" in node and not re.search(node["pattern"], value):
                self.fail(where, f"does not match {node['pattern']}")
        elif kind == "integer":
            if value < node.get("minimum", value) or value > node.get("maximum", value):
                self.fail(where, f"{value} out of range")

    def check_object(self, node, value, where):
        properties = node.get("properties", {})
        for name in node.get("required", []):
            if name not in value:
                self.fail(where, f"member {name!r} missing")
        for name, member in value.items():
            if name in properties:
                self.check(properties[name], member, f"{where}.{name}")
            elif node.get("additionalProperties", True) is False:
                self.fail(where, f"unknown member {name!r}")

    def check_array(self, node, value, where):
        if len(value) < node.get("minItems", 0) or len(value) > node.get("maxItems", len(value)):
            self.fail(where, "wrong number of items")
        prefix = node.get("prefixItems", [])
        for i, item in enumerate(value):
            if i < len(prefix):
                self.check(prefix[i], item, f"{where}[{i}]")
            elif "items" in node:
                self.check(node["items"], item, f"{where}[{i}]")


def check_rules(hints, name, number):
    errors = []
    if hints.get("picture") != number:
        errors.append(f"{name}: the file is named for another picture")
    ids = [item["id"] for key in ("regions", "chains", "gaps") for item in hints.get(key, [])]
    errors += [f"{name}: id {i!r} is used twice" for i in sorted({i for i in ids if ids.count(i) > 1})]
    lines = [line for chain in hints.get("chains", []) for line in chain["lines"]]
    errors += [f"{name}: line {n} is in two chains" for n in sorted({n for n in lines if lines.count(n) > 1})]
    for gap in hints.get("gaps", []):
        if gap["from"] == gap["to"]:
            errors.append(f"{name}: gap {gap['id']!r} joins a line to itself")
    chains = {chain["id"] for chain in hints.get("chains", [])}
    for i, symmetry in enumerate(hints.get("symmetry", [])):
        refs = symmetry["chains"]
        if len(refs) != len(set(refs)) or not set(refs) <= chains:
            errors.append(f"{name}: symmetry[{i}] must list chains of the file, each once")
    return errors


def main():
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "hints"
    schema = json.loads((ROOT / "hints" / "schema.json").read_text())
    errors = []
    files = sorted(p for p in directory.glob("*.json") if p.name != "schema.json")
    for path in files:
        match = FILE_NAME.match(path.name)
        if not match:
            errors.append(f"{path.name}: hint files are named pic.NNN.json")
            continue
        try:
            text = path.read_bytes().decode("ascii")
            hints = json.loads(
                text, object_pairs_hook=reject_duplicates, parse_float=reject_floats, parse_constant=reject_floats
            )
        except (UnicodeDecodeError, ValueError) as error:
            errors.append(f"{path.name}: {error}")
            continue
        validator = Validator(schema)
        validator.check(schema, hints, path.name)
        errors += validator.errors
        if not validator.errors:
            errors += check_rules(hints, path.name, int(match.group(1)))
    for error in errors:
        print(error)
    print(f"{len(files)} hint files, {len(errors)} problems")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
