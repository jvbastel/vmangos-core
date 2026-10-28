"""The pack order of a route on the dungeon map (ticket jvbastel/WOWAI#155):
the hand pack order file of #147 (<route>.order.txt beside the route file),
read into the load-map call and written by the save-order call. The page
part is pack_order.js.

    POST /api/maps/{route}/order   {"packs": [pack key, ...]}: write the order file

The order file has one `pack <spawn guid>` line per pack, in the order to
fight them (src/bots/RouteGenerator.h, ParsePackOrder). A guid names the
pack that holds that mob; this tool writes the pack key, the spawn guid of
the pack's first mob. The packs that the file does not name keep the
generator's order. Saving writes only the order file of the open route,
never the route file.
"""

from __future__ import annotations

from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from pydantic import BaseModel

HERE = Path(__file__).resolve().parent

HEADER = """\
# The hand pack order of {route}.txt (WOWAI#147), saved on the dungeon map
# (WOWAI#155). The packs named here come in this order, from where the route
# meets the first of them. The other packs keep the order of the generator.
# A pack is named by the spawn guid of its first mob: guids stay the same at
# each regen, pack names do not. The comments give the pack names of the
# route at the save.
"""


class PackOrderError(Exception):
    pass


def file_name(route_name: str) -> str:
    return f"{route_name}.order.txt"


def parse(text: str) -> list[tuple[int, int]]:
    """The spawn guids of an order file, in its order, each with its line
    number. The rules of the route library's ParsePackOrder: `#` starts a
    comment, a guid is 1 to 2^32 - 1 and comes once."""
    guids: list[int] = []
    lines: list[int] = []
    for number, raw in enumerate(text.splitlines(), 1):
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        guid = int(words[1]) if len(words) == 2 and words[0] == "pack" and words[1].isdigit() else 0
        if not 0 < guid <= 0xFFFFFFFF:
            raise PackOrderError(f"line {number}: use: pack <spawn guid>")
        if guid in guids:
            raise PackOrderError(f"line {number}: the guid {words[1]} is already in the order")
        guids.append(guid)
        lines.append(number)
    return list(zip(lines, guids))


def merge_shared_keys(packs: list[dict]) -> tuple[list[dict], list[str]]:
    """One pack per key. A stale route can hold two packs that are one pack
    of the export (map_packs gives both its key): the first keeps its place
    and gains the mobs of the others, with a warning."""
    merged: dict = {}
    warnings = []
    for pack in packs:
        first = merged.get(pack["key"])
        if first is None:
            merged[pack["key"]] = {**pack, "mobs": list(pack["mobs"])}
            continue
        first["mobs"] += [m for m in pack["mobs"] if m not in first["mobs"]]
        warnings.append(f"the route packs {first['label']} and {pack['label']} are one pack of the export "
                        f"(key {pack['key']}): shown as {first['label']}")
    return list(merged.values()), warnings


def map_part(name: str, packs: dict, routes: Path) -> dict:
    """The packs of the map with one pack per key, and the current pack
    order of the route: the hand part (the packs of the order file, in its
    order) and the generator's part (the other packs of the route, in route
    order). `name` is the route file's name, `packs` map_packs.map_part's
    result; the load-map call puts this part after it, so these packs
    replace its packs."""
    path = routes / file_name(name)
    merged, warnings = merge_shared_keys(packs["packs"])
    pack_of = {m["guid"]: p["key"] for p in merged for m in p["mobs"]}
    hand: list[int] = []
    if path.is_file():
        try:
            guids = parse(path.read_text())
        except PackOrderError as error:
            raise HTTPException(422, f"{path}: {error}")
        for number, guid in guids:
            key = pack_of.get(guid)
            where = f"{path.name}: line {number}: the guid {guid}"
            if key is None:
                warnings.append(f"{where} is not a mob of a pack of the map; left out")
            elif key in hand:
                warnings.append(f"{where} is in pack {key}, already at place {hand.index(key) + 1}; left out")
            else:
                hand.append(key)
    on_route = sorted((p for p in merged if p["on_route"]), key=lambda p: p["order"])
    generator = [p["key"] for p in on_route if p["key"] not in hand]
    return {"packs": merged, "pack_order": {"file": path.name, "exists": path.is_file(), "hand": hand, "generator": generator,
                           "warnings": warnings}}


def write(route_name: str, keys: list[int], packs: list[dict]) -> str:
    """The text of the order file: one `pack <key>` line per pack, with its
    route name and its mobs as a comment."""
    by_key = {p["key"]: p for p in packs}
    lines = [HEADER.format(route=route_name)]
    for key in keys:
        pack = by_key[key]
        names = ", ".join(m["name"] or str(m["entry"]) for m in pack["mobs"])
        lines.append(f"pack {key} # {pack['label'] or 'off the route'}, {names}\n")
    return "".join(lines)


class SavedOrder(BaseModel):
    packs: list[int]  # the pack keys of the hand order, in order


def add_routes(app: FastAPI, settings, load_route, load_packs):
    """The save-order call and the page part. load_packs(route) is the
    map_packs part of the route's map."""
    @app.get("/pack_order.js")
    def script():
        return FileResponse(HERE / "pack_order.js", media_type="text/javascript")

    @app.post("/api/maps/{name}/order")
    def save(name: str, order: SavedOrder):
        route = load_route(name)
        packs, _ = merge_shared_keys(load_packs(route)["packs"])
        known = {p["key"] for p in packs}
        for key in order.packs:
            if key not in known:
                raise HTTPException(422, f"{key} is not the key of a pack of the map")
        if len(set(order.packs)) != len(order.packs):
            raise HTTPException(422, "a pack comes twice in the order")
        path = settings.routes / file_name(name)
        path.write_text(write(name, order.packs, packs))
        return {"file": path.name, "packs": len(order.packs)}
