"""Every pack of the dungeon on the map (ticket jvbastel/WOWAI#153): the
packs of the map's export file, with the packs of the route marked, plus
the patrols, the wanderers and the bosses. map_web's load-map call adds
map_part to its result; the page part is map_packs.js.

A pack is known by its key, the spawn guid of its first mob (#147): the
same pack in the export, the route and the hand pack order file. Its `packN`
label is the route's name for it, only for a pack on the route.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from fastapi import FastAPI
from fastapi.responses import FileResponse

import export_file
import route_file

# The generator's aggro range (GeneratorSettings::aggroRange, src/bots/
# RouteGenerator.h): how near a pack notices a player of the dungeon level.
AGGRO_RADIUS = 20.0


def _xyz(x, y, z):
    return {"x": x, "y": y, "z": z}


def _route_pack(route: route_file.Route, pack: route_file.Pack, key: int, order: int, bosses: list[str]):
    pull = route.pull_point(pack)
    return {"key": key, "label": pack.name, "order": order, "on_route": True,
            "x": pack.x, "y": pack.y, "z": pack.z, "mobs": [vars(m) for m in pack.mobs],
            "aggro_radius": AGGRO_RADIUS, "bosses": bosses, "pull_point": vars(pull) if pull else None}


def _off_route_pack(pack: export_file.ExportPack, names: dict[int, str], bosses: list[str]):
    first = pack.mobs[0]
    return {"key": pack.key, "label": None, "order": None, "on_route": False,
            "x": first.x, "y": first.y, "z": first.z,
            "mobs": [{"guid": m.guid, "entry": m.entry, "name": names.get(m.entry, "")} for m in pack.mobs],
            "aggro_radius": AGGRO_RADIUS, "bosses": bosses, "pull_point": None}


def map_part(route: route_file.Route, export: Optional[export_file.MapExport]) -> dict:
    """The packs (the route's first, in route order, then the others in
    export order), the patrols, the wanderers and the bosses of the map."""
    export = export or export_file.MapExport(route.map_id)
    names = dict(export.creature_names)
    for pack in route.packs:
        for mob in pack.mobs:
            names.setdefault(mob.entry, mob.name)
    boss_entries = set(export.bosses)

    def boss_names(entries):
        return [names.get(e, str(e)) for e in dict.fromkeys(entries) if e in boss_entries]

    # A route pack is the export pack that holds one of its mobs, as the
    # library names a pack by any of its mobs. A route file written before
    # #147 can list another mob first; the export's key is the one kept.
    key_of_guid = {mob.guid: pack.key for pack in export.packs for mob in pack.mobs}

    def key_of(pack):
        return next((key_of_guid[m.guid] for m in pack.mobs if m.guid in key_of_guid), pack.key)

    # Every export pack that holds a mob of the route is on the route, also
    # when one route pack spans two export packs.
    on_route = {key_of_guid.get(m.guid, pack.key) for pack in route.packs for m in pack.mobs}
    packs = [_route_pack(route, pack, key_of(pack), order, boss_names(m.entry for m in pack.mobs))
             for order, pack in enumerate(route.packs, 1)]
    packs += [_off_route_pack(pack, names, boss_names(m.entry for m in pack.mobs))
              for pack in export.packs if pack.key not in on_route and pack.mobs]

    def mob_view(pack, mob):
        return {"guid": mob.guid, "entry": mob.entry, "name": names.get(mob.entry, ""), "pack": pack.key}

    members = [(pack, mob) for pack in export.packs for mob in pack.mobs]
    return {
        "packs": packs,
        "patrols": [{**mob_view(p, m), "points": [_xyz(*w) for w in m.patrol]} for p, m in members if m.patrol],
        "wanderers": [{**mob_view(p, m), **_xyz(m.x, m.y, m.z), "radius": m.wander}
                      for p, m in members if m.wander > 0 and not m.patrol],
        "bosses": [{**{k: v for k, v in mob_view(p, m).items() if k != "guid"}, **_xyz(m.x, m.y, m.z)}
                   for entry in export.bosses for p, m in members if m.entry == entry],
    }


def add_routes(app: FastAPI):
    """Serves the page part, map_packs.js."""
    @app.get("/map_packs.js")
    def script():
        return FileResponse(Path(__file__).resolve().parent / "map_packs.js", media_type="text/javascript")
