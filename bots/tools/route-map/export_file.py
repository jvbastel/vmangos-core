"""The reader of a map's export file (routes/map_<id>.export.txt), written by
`.route export` in the format of the route library (src/bots/RouteExport.h):
every pack of the map with its mobs, patrol waypoints, wander distances, the
bosses, the quest objectives, and the names of creatures, quests and items.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


class ExportFileError(Exception):
    pass


@dataclass
class ExportMob:
    guid: int
    entry: int
    x: float
    y: float
    z: float
    wander: float
    patrol: list[tuple[float, float, float]] = field(default_factory=list)


@dataclass
class ExportPack:
    key: int  # the guid of its first mob
    mobs: list[ExportMob] = field(default_factory=list)


@dataclass
class ExportQuest:
    quest: int
    kills: list[tuple[int, int]] = field(default_factory=list)  # (entry, count)
    items: list[tuple[int, int, list[int]]] = field(default_factory=list)  # (item, count, dropped by entries)
    visits: list[tuple[float, float, float, str]] = field(default_factory=list)  # (x, y, z, name)


@dataclass
class MapExport:
    map_id: int
    bosses: list[int] = field(default_factory=list)  # entries, in run order
    packs: list[ExportPack] = field(default_factory=list)
    quests: list[ExportQuest] = field(default_factory=list)
    creature_names: dict[int, str] = field(default_factory=dict)
    quest_names: dict[int, str] = field(default_factory=dict)
    item_names: dict[int, str] = field(default_factory=dict)


def file_name(map_id: int) -> str:
    """As Bots::ExportFileName."""
    return f"map_{map_id}.export.txt"


def load(folder: Path, map_id: int) -> Optional[MapExport]:
    """The export of the map in the folder, or None when there is none."""
    path = folder / file_name(map_id)
    if not path.is_file():
        return None
    try:
        return parse(path.read_text())
    except ExportFileError as error:
        raise ExportFileError(f"{path}: {error}")


def parse(text: str) -> MapExport:
    export: Optional[MapExport] = None
    for number, raw in enumerate(text.splitlines(), 1):
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        kind, rest = words[0], words[1:]
        name = " ".join(rest[1:])

        def last(items, what):
            if not items:
                raise ExportFileError(f"line {number}: a {kind} line before any {what} line")
            return items[-1]

        try:
            if kind == "export":
                export = MapExport(int(rest[0]))
                continue
            if export is None:
                raise ExportFileError(f"line {number}: the first line is not `export <map id>`")
            if kind == "boss":
                export.bosses.append(int(rest[0]))
            elif kind == "pack":
                export.packs.append(ExportPack(int(rest[0])))
            elif kind == "mob":
                last(export.packs, "pack").mobs.append(
                    ExportMob(int(rest[0]), int(rest[1]), *map(float, rest[2:6])))
            elif kind == "wp":
                mobs = last(export.packs, "pack").mobs
                x, y, z = map(float, rest[0:3])
                last(mobs, "mob").patrol.append((x, y, z))
            elif kind == "quest":
                export.quests.append(ExportQuest(int(rest[0])))
            elif kind == "kill":
                last(export.quests, "quest").kills.append((int(rest[0]), int(rest[1])))
            elif kind == "item":
                last(export.quests, "quest").items.append((int(rest[0]), int(rest[1]), [int(w) for w in rest[2:]]))
            elif kind == "visit":
                x, y, z = map(float, rest[0:3])
                last(export.quests, "quest").visits.append((x, y, z, " ".join(rest[3:])))
            elif kind == "creature":
                export.creature_names[int(rest[0])] = name
            elif kind == "questname":
                export.quest_names[int(rest[0])] = name
            elif kind == "itemname":
                export.item_names[int(rest[0])] = name
            else:
                raise ExportFileError(f"line {number}: unknown line {raw.strip()!r}")
        except (IndexError, ValueError):
            raise ExportFileError(f"line {number}: cannot read {raw.strip()!r}")
    if export is None:
        raise ExportFileError("no `export <map id>` line")
    return export
