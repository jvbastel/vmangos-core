"""The SpellIconIDs that the server core treats as spell markers.

The core finds some spells by their icon (for example `SpellIconID == 108` for Flurry). A new or changed
Forever spell with such an icon could trigger that stock code by accident, so the generator refuses it.
The list comes from the core source itself, so that it follows the core: each comparison
`SpellIconID == N` (also !=, <, >, <=, >=) and each top-level `case N:` of a `switch (... SpellIconID)`.
"""
import os
import re

COMPARISON = re.compile(r"SpellIconID\s*(?:==|!=|>=|<=|>|<)\s*(\d+)")
SWITCH = re.compile(r"switch\s*\([^)]*SpellIconID\s*\)\s*\{")
CASE = re.compile(r"case\s+(\d+)\s*:")


def _switch_cases(source, start):
    """The case labels at the top level of the switch body that starts at `start` (after its brace)."""
    depth, i = 1, start
    while depth and i < len(source):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    cases, depth = set(), 0
    for line in source[start:i].split("\n"):
        if depth == 0:
            cases.update(int(c) for c in CASE.findall(line))
        depth += line.count("{") - line.count("}")
    return cases


def icons_in(source):
    icons = {int(m) for m in COMPARISON.findall(source)}
    for m in SWITCH.finditer(source):
        icons |= _switch_cases(source, m.end())
    return icons


def scan(core_src):
    """The marker icons of every C++ file under `core_src` (the src folder of the core)."""
    if not os.path.isdir(core_src):
        raise FileNotFoundError("no core source folder at %s" % core_src)
    icons = set()
    for root, dirs, names in os.walk(core_src):
        dirs.sort()
        for name in sorted(names):
            if name.endswith((".cpp", ".h", ".hpp")):
                with open(os.path.join(root, name), encoding="utf-8", errors="replace") as file:
                    source = file.read()
                if "SpellIconID" in source:
                    icons |= icons_in(source)
    return icons
