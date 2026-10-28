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

from fastapi import HTTPException


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


def map_part(route, packs: dict, routes: Path) -> dict:
    """The packs of the map with one pack per key, and the current pack
    order of the route: the hand part (the packs of the order file, in its
    order) and the generator's part (the other packs of the route, in route
    order). `packs` is map_packs.map_part's result; the load-map call puts
    this part after it, so these packs replace its packs."""
    path = routes / file_name(route.name)
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
