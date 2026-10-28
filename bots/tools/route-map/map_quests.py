"""The quest objectives of the dungeon on the map (ticket jvbastel/WOWAI#154):
the quests of the map's export file, each objective with what the route
meets and, when it does not, the packs off the route that could meet it.
map_web's load-map call adds map_part to its result; the page part is
map_quests.js.

- A kill objective is met when the route's packs hold at least the needed
  count of the mob.
- An item objective is met when the route's packs hold at least the needed
  count of mobs that drop it (a drop is not sure; this counts chances).
- A visit objective is met when a point of the route line is within
  VISIT_RANGE of the visit point.

The packs are those of map_packs.map_part: the route's packs (on_route) with
the route's mobs, and the other export packs with theirs.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Optional

from fastapi import FastAPI
from fastapi.responses import FileResponse

import export_file
import route_file

# How near a route point must come to a visit point. The quest visits of a
# dungeon are objects to use (a corpse, a satchel): the interact range.
VISIT_RANGE = 5.0


def _counts(packs: list[dict], entries: set[int]) -> list[tuple[dict, int]]:
    """(pack, how many of its mobs are of the entries), for each pack with one."""
    counted = [(pack, sum(m["entry"] in entries for m in pack["mobs"])) for pack in packs]
    return [(pack, n) for pack, n in counted if n]


def _mob_objective(kind: str, entries: set[int], needed: int, packs: list[dict], view: dict) -> dict:
    counted = _counts(packs, entries)
    on_route = sum(n for pack, n in counted if pack["on_route"])
    return {"kind": kind, **view, "needed": needed, "on_route": on_route, "met": on_route >= needed,
            "packs": [{"key": pack["key"], "count": n} for pack, n in counted if not pack["on_route"]]}


def _visit_objective(visit, line: list[route_file.Point]) -> dict:
    x, y, z, name = visit
    met = any(math.dist((p.x, p.y, p.z), (x, y, z)) <= VISIT_RANGE for p in line)
    return {"kind": "visit", "name": name, "x": x, "y": y, "z": z, "met": met, "packs": []}


def map_part(route: route_file.Route, export: Optional[export_file.MapExport], packs: list[dict]) -> dict:
    """The quests of the export, in export order, each with its objectives."""
    export = export or export_file.MapExport(route.map_id)
    names = export.creature_names

    def creature(entry):
        return names.get(entry, str(entry))

    quests, badges = [], []
    for quest in export.quests:
        objectives = [_mob_objective("kill", {entry}, count, packs, {"entry": entry, "name": creature(entry)})
                      for entry, count in quest.kills]
        objectives += [_mob_objective("item", set(droppers), count, packs, {
                           "item": item, "name": export.item_names.get(item, str(item)),
                           "dropped_by": [{"entry": e, "name": creature(e)} for e in droppers]})
                       for item, count, droppers in quest.items]
        objectives += [_visit_objective(visit, route.line) for visit in quest.visits]
        name = export.quest_names.get(quest.quest, "")
        quests.append({"quest": quest.quest, "name": name,
                       "met": all(o["met"] for o in objectives), "objectives": objectives})
        badges += _badges(f"{quest.quest} {name}".strip(), quest, packs, creature, export.item_names)
    return {"quests": quests, "quest_badges": badges}


def _badges(title: str, quest: export_file.ExportQuest, packs: list[dict], creature, item_names) -> list[dict]:
    """A badge on each pack for each kill or item objective of the quest that
    its mobs count for, with the hover text of the page."""
    badges = []

    def badge(pack, count, needed, text):
        badges.append({"pack": pack["key"], "quest": quest.quest, "count": count, "needed": needed,
                       "text": f"{title}: {text}"})

    for entry, needed in quest.kills:
        for pack, n in _counts(packs, {entry}):
            badge(pack, n, needed, f"{n} of {needed} {creature(entry)} here")
    for item, needed, droppers in quest.items:
        for pack, n in _counts(packs, set(droppers)):
            mobs = ", ".join(dict.fromkeys(creature(m["entry"]) for m in pack["mobs"] if m["entry"] in droppers))
            badge(pack, n, needed, f"{n} {mobs} here drop {item_names.get(item, item)} ({needed} needed)")
    return badges


def add_routes(app: FastAPI):
    """Serves the page part, map_quests.js."""
    @app.get("/map_quests.js")
    def script():
        return FileResponse(Path(__file__).resolve().parent / "map_quests.js", media_type="text/javascript")
