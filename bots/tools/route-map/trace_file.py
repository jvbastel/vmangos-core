"""The trace file of a route (bots/data/routes/<route>.trace.txt), the
format of the route library (src/bots/RouteTrace.h): `trace <name> <map>`,
then `p <x> <y> <z>` points in walk order. `.route build` makes the route
file from it.
"""

from __future__ import annotations


def write(name: str, map_id: int, points) -> str:
    """The text of a trace of joined points, as ParseTrace reads it."""
    lines = [f"# The trace of the route {name}, drawn on the dungeon map: .route build makes the route.",
             f"trace {name} {map_id}"]
    lines += [f"p {x:.2f} {y:.2f} {z:.2f}" for x, y, z in points]
    return "\n".join(lines) + "\n"
