"""The trace file of a route (bots/data/routes/<route>.trace.txt), the
format of the route library (src/bots/RouteTrace.h, ParseTrace and
WriteTrace):

    trace <name> <map>      first, once
    p <x> <y> <z>           a trace point, in walk order
    mark <x> <y> <z> [note] a checkpoint
    gap                     the next point does not join the one before
    walk                    the next point starts a later walk

`.route build` makes the route file from it. The reader follows the rules
of ParseTrace, so a file that it reads, the library reads too.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field


class TraceFileError(Exception):
    pass


@dataclass
class TracePoint:
    x: float
    y: float
    z: float
    joined: bool = True  # walked from the point before
    walk: bool = False  # the first point of a later walk
    mark: bool = False
    note: str = ""


@dataclass
class Trace:
    name: str
    map_id: int
    points: list[TracePoint] = field(default_factory=list)


def _number(word):
    value = float(word)
    if not math.isfinite(value):
        raise ValueError(word)
    return value


def parse(text: str) -> Trace:
    trace = None
    gap = walk = False
    for number, raw in enumerate(text.splitlines(), 1):
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        key = words[0]

        def fail(message):
            return TraceFileError(f"line {number}: {message}")

        if key == "trace":
            if trace is not None:
                raise fail("a second trace line")
            if len(words) != 3 or not words[2].isdigit():
                raise fail("use trace <name> <map>")
            trace = Trace(words[1], int(words[2]))
            continue
        if trace is None:
            raise fail("the file must start with trace <name> <map>")
        if key in ("gap", "walk"):
            if len(words) != 1:
                raise fail(f"{key} takes no words")
            gap, walk = True, walk or key == "walk"
            continue
        if key not in ("p", "mark"):
            raise fail(f"unknown line {key}")
        try:
            if len(words) < 4 or (key == "p" and len(words) != 4):
                raise ValueError(key)
            x, y, z = map(_number, words[1:4])
        except ValueError:
            raise fail(f"use {key} <x> <y> <z>" + (" [note]" if key == "mark" else ""))
        trace.points.append(TracePoint(x, y, z, joined=not gap and bool(trace.points),
                                       walk=walk and bool(trace.points), mark=key == "mark",
                                       note=" ".join(words[4:])))
        gap = walk = False
    if trace is None:
        raise TraceFileError("the file has no trace line")
    return trace


def write(name: str, map_id: int, points) -> str:
    """The text of a trace of joined points (x, y, z), as ParseTrace reads it."""
    lines = [f"# The trace of the route {name}, drawn on the dungeon map: .route build makes the route.",
             f"trace {name} {map_id}"]
    lines += [f"p {x:.2f} {y:.2f} {z:.2f}" for x, y, z in points]
    return "\n".join(lines) + "\n"
