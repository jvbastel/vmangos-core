"""The regions of a dungeon map (ticket jvbastel/WOWAI#182, spec #179, ADR
0004): hand-drawn outlines, each with a height range, that hold one layer of
stacked ground. The regions of a map are shared by all its routes. This
module owns the regions file of a map: it reads it, checks it and writes it,
and it answers whether a position is in a region. The page part is
map_regions.js.

    GET  /api/maps/{route}/regions   the regions of the route's map: {"file", "exists", "regions"}
    POST /api/maps/{route}/regions   {"regions": [{name, low, high, outline: [[x, y], ...]}, ...]}:
                                     write the regions file of the route's map

The regions file is routes/map_<id>.regions.txt, beside the map's export.
One line per region:

    region <low> <high> <x1> <y1> <x2> <y2> <x3> <y3> ... name <name>

The heights and the outline points are in yards (WoW positions); the name
is the rest of the line after `name` and may hold spaces. `#` starts a
comment. A region is valid when its name is not empty and unique in the
map, its outline has at least 3 points, and its low height is below its
high height. A save keeps the comments of the old file (see write).
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from pydantic import BaseModel

HERE = Path(__file__).resolve().parent

HEADER = """\
# The regions of map {map_id}, saved on the dungeon map (WOWAI#179, #182).
# A region is an outline with a height range: it holds one layer of stacked
# ground. Every route of the map shares them; they do not change a route.
# One line per region: region <low> <high> <x y of each outline point> name <name>
"""


class RegionsError(Exception):
    pass


@dataclass
class Region:
    name: str
    low: float
    high: float
    outline: list[tuple[float, float]]

    def holds(self, x: float, y: float, z: float) -> bool:
        """The position is inside the outline and inside the height range,
        borders and ends included."""
        return self.low <= z <= self.high and inside(self.outline, x, y)


def inside(outline: list[tuple[float, float]], x: float, y: float) -> bool:
    """(x, y) is inside the polygon or on its border."""
    n = len(outline)
    hit = False
    for k in range(n):
        (ax, ay), (bx, by) = outline[k], outline[(k + 1) % n]
        # On the border: on the segment a-b (collinear and between its ends).
        cross = (bx - ax) * (y - ay) - (by - ay) * (x - ax)
        if abs(cross) <= 1e-9 * max(1.0, math.hypot(bx - ax, by - ay)) \
                and min(ax, bx) - 1e-9 <= x <= max(ax, bx) + 1e-9 and min(ay, by) - 1e-9 <= y <= max(ay, by) + 1e-9:
            return True
        if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
            hit = not hit
    return hit


def regions_at(regions: list[Region], x: float, y: float, z: float) -> list[str]:
    """The names of the regions that hold the position, in file order. A
    position on the border of two regions is in both."""
    return [r.name for r in regions if r.holds(x, y, z)]


def file_name(map_id: int) -> str:
    return f"map_{map_id}.regions.txt"


def _parse_line(words: list[str]) -> Region:
    if "name" not in words:
        raise RegionsError("no `name <name>` at the end")
    at = words.index("name")
    numbers = [float(w) for w in words[1:at]]
    if any(not math.isfinite(v) for v in numbers):
        raise ValueError
    if len(numbers) < 2 or len(numbers) % 2:
        raise RegionsError("give the low and high heights, then x y pairs")
    low, high, rest = numbers[0], numbers[1], numbers[2:]
    return Region(" ".join(words[at + 1:]), low, high, list(zip(rest[0::2], rest[1::2])))


def parse(text: str) -> list[Region]:
    """The regions of a regions file, in its order, each checked."""
    regions: list[Region] = []
    for number, raw in enumerate(text.splitlines(), 1):
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        try:
            if words[0] != "region":
                raise RegionsError(f"use: region <low> <high> <x1> <y1> ... name <name>")
            regions.append(_parse_line(words))
            check(regions)
        except ValueError:
            raise RegionsError(f"line {number}: cannot read {raw.strip()!r}")
        except RegionsError as error:
            raise RegionsError(f"line {number}: {error}")
    return regions


def check(regions: list[Region]):
    """Raise RegionsError for the first region that is not valid."""
    seen: set[str] = set()
    for region in regions:
        where = f"region {region.name!r}"
        if not region.name.strip():
            raise RegionsError("a region has no name")
        if "#" in region.name or "\n" in region.name or region.name != region.name.strip():
            raise RegionsError(f"{where}: a name has no # and no line break, and no space at either end")
        if region.name in seen:
            raise RegionsError(f"{where}: two regions have this name")
        seen.add(region.name)
        if len(region.outline) < 3:
            raise RegionsError(f"{where}: the outline has {len(region.outline)} points; it needs at least 3")
        if not region.low < region.high:
            raise RegionsError(f"{where}: the low height {region.low:g} is not below the high height {region.high:g}")


def _number(value: float) -> str:
    return f"{value:.2f}"


def line(region: Region) -> str:
    points = " ".join(f"{_number(x)} {_number(y)}" for x, y in region.outline)
    return f"region {_number(region.low)} {_number(region.high)} {points} name {region.name}"


def _code(raw: str) -> str:
    return raw.split("#", 1)[0].strip()


def _strip_blank(lines: list[str]) -> list[str]:
    while lines and not lines[0].strip():
        lines = lines[1:]
    while lines and not lines[-1].strip():
        lines = lines[:-1]
    return lines


def layout(text: str) -> tuple[list[str], list[tuple[str, str, list[str]]], list[str]]:
    """The comments of a regions file: its header (the lines above the last
    blank line before the first region line), each region's name with the
    comment at the end of its line and the comment block above it (the
    lines since the region line before, or since the header), and the lines
    after the last region line."""
    lines = text.splitlines()
    at = [i for i, raw in enumerate(lines) if _code(raw)]
    if not at:
        return _strip_blank(lines), [], []
    leading = lines[:at[0]]
    blanks = [i for i, raw in enumerate(leading) if not raw.strip()]
    header, first = (leading[:blanks[-1]], leading[blanks[-1] + 1:]) if blanks else (leading, [])
    found = []
    for n, i in enumerate(at):
        block = first if n == 0 else lines[at[n - 1] + 1:i]
        code = lines[i].split("#", 1)[0]
        name = _parse_line(code.split()).name
        found.append((name, lines[i][len(code.rstrip()):], _strip_blank(block)))
    return _strip_blank(header), found, _strip_blank(lines[at[-1] + 1:])


def write(map_id: int, regions: list[Region], old_text: str | None = None) -> str:
    """The text of the regions file: one line per region. The comments of
    the old file (it must read) stay: its header at the top, the comment
    block above a region line and the comment at its end with the region of
    that name, and the blocks of regions that are gone and the lines after
    the last region at the end."""
    header, old, footer = layout(old_text) if old_text is not None else ([], [], [])
    names = {r.name for r in regions}
    kept = {name: (comment, block) for name, comment, block in old if name in names}
    gone = [block for name, _, block in old if name not in names and block]
    out = list(header or HEADER.format(map_id=map_id).splitlines())
    for n, region in enumerate(regions):
        comment, block = kept.get(region.name, ("", []))
        if block:
            out += [""] + block
        elif n == 0:
            out += [""]
        out.append(line(region) + comment)
    for block in gone + ([footer] if footer else []):
        out += [""] + block
    return "\n".join(_strip_blank(out)) + "\n"


def load(folder: Path, map_id: int) -> list[Region]:
    """The regions of the map, none when it has no regions file."""
    path = folder / file_name(map_id)
    if not path.is_file():
        return []
    try:
        return parse(path.read_text())
    except RegionsError as error:
        raise RegionsError(f"{path}: {error}")


class SavedRegion(BaseModel):
    name: str
    low: float
    high: float
    outline: list[tuple[float, float]]


class SavedRegions(BaseModel):
    regions: list[SavedRegion]


def view(region: Region) -> dict:
    return {"name": region.name, "low": region.low, "high": region.high,
            "outline": [[x, y] for x, y in region.outline]}


def add_routes(app: FastAPI, maps):
    """The regions calls and the page part. maps is map_web's MapData."""
    @app.get("/map_regions.js")
    def script():
        return FileResponse(HERE / "map_regions.js", media_type="text/javascript")

    def answer(map_id: int, regions: list[Region]) -> dict:
        name = file_name(map_id)
        return {"file": name, "exists": (maps.settings.routes / name).is_file(),
                "regions": [view(r) for r in regions]}

    @app.get("/api/maps/{name}/regions")
    def load_regions(name: str):
        map_id = maps.route(name).map_id
        try:
            return answer(map_id, load(maps.settings.routes, map_id))
        except RegionsError as error:
            raise HTTPException(422, str(error))

    @app.post("/api/maps/{name}/regions")
    def save_regions(name: str, ask: SavedRegions):
        map_id = maps.route(name).map_id
        path = maps.settings.routes / file_name(map_id)
        regions = [Region(r.name, round(r.low, 2), round(r.high, 2),
                          [(round(x, 2), round(y, 2)) for x, y in r.outline]) for r in ask.regions]
        old_text = path.read_text() if path.is_file() else None
        try:
            check(regions)
            if old_text is not None:
                try:
                    parse(old_text)
                except RegionsError as error:
                    raise RegionsError(f"{path}: {error}")
        except RegionsError as error:
            raise HTTPException(422, f"not saved: {error}")
        part = path.with_suffix(".txt.part")  # write, then rename: a load never reads half a file
        part.write_text(write(map_id, regions, old_text))
        part.replace(path)
        return answer(map_id, regions)
