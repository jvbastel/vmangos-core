"""The reader of a route file (bots/data/routes/<route>.txt), the format of
the route library (src/bots/Route.h). The map needs only part of it: the map
id, the route line (the `point` lines, not their `alt` spots), and the packs
with their mobs. A pull point is the point `pull_<pack>`.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


class RouteFileError(Exception):
    pass


@dataclass
class Point:
    name: str
    x: float
    y: float
    z: float


@dataclass
class Mob:
    guid: int
    entry: int
    name: str


@dataclass
class Pack:
    name: str
    x: float
    y: float
    z: float
    mobs: list[Mob] = field(default_factory=list)

    @property
    def key(self) -> Optional[int]:
        """The pack key: the spawn guid of the pack's first mob (#147)."""
        return self.mobs[0].guid if self.mobs else None


@dataclass
class Route:
    name: str
    map_id: int
    line: list[Point]
    packs: list[Pack]

    def pull_point(self, pack: Pack) -> Optional[Point]:
        return next((p for p in self.line if p.name == f"pull_{pack.name}"), None)


def parse(text: str) -> Route:
    name, map_id = None, None
    line: list[Point] = []
    packs: list[Pack] = []
    for number, raw in enumerate(text.splitlines(), 1):
        comment = raw.split("#", 1)[1].strip() if "#" in raw else ""
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        try:
            if words[0] == "route":
                name, map_id = words[1], int(words[2])
            elif words[0] == "point":
                line.append(Point(words[1], *map(float, words[2:5])))
            elif words[0] == "pack":
                packs.append(Pack(words[1], *map(float, words[2:5])))
            elif words[0] == "mob":
                if not packs:
                    raise RouteFileError(f"line {number}: a mob line before any pack line")
                packs[-1].mobs.append(Mob(int(words[1]), int(words[2]), comment))
        except (IndexError, ValueError):
            raise RouteFileError(f"line {number}: cannot read {raw.strip()!r}")
    if name is None:
        raise RouteFileError("no `route <name> <map id>` line")
    return Route(name, map_id, line, packs)
