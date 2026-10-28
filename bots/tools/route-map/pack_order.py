"""The pack order of a route on the dungeon map (ticket jvbastel/WOWAI#155):
the hand pack order file of #147 (<route>.order.txt beside the route file),
read into the load-map call and written by the save-order call. The page
part is pack_order.js.

    POST /api/maps/{route}/order   {"packs": [pack key, ...], "skips": [pack key, ...]}: write the order file

The order file has one `pack <spawn guid>` line per pack, in the order to
fight them, and one `skip <spawn guid>` line per skipped pack (src/bots/
RouteGenerator.h, ParsePackOrder; src/bots/RouteHandOrder.h). A `pack` line
of a pack that is not on the route adds it to the route (#156). A guid names
the pack that holds that mob; this tool writes the pack key, the spawn guid
of the pack's first mob. The packs that the file does not name keep the
generator's order. Saving writes only the order file of the open route,
never the route file. The hand comments of the old order file stay (see write).
"""

from __future__ import annotations

import math
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from pydantic import BaseModel

import export_file

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


def parse(text: str) -> list[tuple[int, str, int]]:
    """The lines of an order file, in its order: (line number, "pack" or
    "skip", spawn guid). The rules of the route library's ParsePackOrder:
    `#` starts a comment, a guid is 1 to 2^32 - 1 and comes once in the
    whole file."""
    entries: list[tuple[int, str, int]] = []
    for number, raw in enumerate(text.splitlines(), 1):
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        ok = len(words) == 2 and words[0] in ("pack", "skip") and words[1].isdigit()
        guid = int(words[1]) if ok else 0
        if not 0 < guid <= 0xFFFFFFFF:
            raise PackOrderError(f"line {number}: use: pack <spawn guid>, or skip <spawn guid>")
        if any(guid == seen for _, _, seen in entries):
            raise PackOrderError(f"line {number}: the guid {words[1]} is already in the order")
        entries.append((number, words[0], guid))
    return entries


def map_part(name: str, packs: list[dict], routes: Path, line: list | None = None,
             export: export_file.MapExport | None = None) -> dict:
    """The current pack order of the route: the hand part (the `pack` lines
    of the order file, in its order; a pack off the route is an added
    pack), the skipped packs (its `skip` lines), the generator's part (the
    other packs of the route, in route order), the packs that the built
    route pulls (on the route or added, and not skipped, as `.route build`
    reads the file), and the skipped packs that the route line (the route
    file's points) comes within the aggro range of. `name` is the route
    file's name, `packs` the pack list of map_packs.map_part."""
    path = routes / file_name(name)
    warnings: list[str] = []
    pack_of = {m["guid"]: p["key"] for p in packs for m in p["mobs"]}
    hand: list[int] = []
    skipped: list[int] = []
    if path.is_file():
        try:
            entries = parse(path.read_text())
        except PackOrderError as error:
            raise HTTPException(422, f"{path}: {error}")
        # The skips first, as the route library's ResolveHandOrder: a skip
        # wins over a `pack` line of the same pack.
        for number, word, guid in sorted(entries, key=lambda entry: entry[1] != "skip"):
            key = pack_of.get(guid)
            where = f"{path.name}: line {number}: the guid {guid}"
            if key is None:
                warnings.append(f"{where} is not a mob of a pack of the map; left out")
            elif key in skipped:
                warnings.append(f"{where} is in pack {key}, which the file skips; it skips it" if word == "pack"
                                else f"{where} is in pack {key}, already skipped; left out")
            elif key in hand:
                warnings.append(f"{where} is in pack {key}, already at place {hand.index(key) + 1}; left out")
            else:
                (skipped if word == "skip" else hand).append(key)
    on_route = sorted((p for p in packs if p["on_route"]), key=lambda p: p["order"])
    generator = [p["key"] for p in on_route if p["key"] not in hand and p["key"] not in skipped]
    pulled = [p["key"] for p in packs if (p["on_route"] or p["key"] in hand) and p["key"] not in skipped]
    near = skipped_near(line or [], skipped, export, {p["key"]: p for p in packs})
    return {"pack_order": {"file": path.name, "exists": path.is_file(), "hand": hand, "skipped": skipped,
                           "generator": generator, "pulled": pulled, "warnings": warnings,
                           "skipped_near": near}}


def _distance(a, b) -> float:
    return math.dist(a, b)


def _to_segment(p, a, b) -> float:
    ab = [b[i] - a[i] for i in range(3)]
    length = sum(c * c for c in ab)
    t = 0.0 if length == 0 else max(0.0, min(1.0, sum((p[i] - a[i]) * ab[i] for i in range(3)) / length))
    return _distance(p, [a[i] + t * ab[i] for i in range(3)])


def _to_mob(p, mob: export_file.ExportMob) -> float:
    """The route library's DistanceToPack for one mob: to its spot less its
    wander distance, or to its patrol path."""
    at = (mob.x, mob.y, mob.z)
    distance = max(0.0, _distance(p, at) - mob.wander)
    for waypoint in mob.patrol:
        distance = min(distance, _to_segment(p, at, waypoint))
        at = waypoint
    return distance


def _to_mob_on_piece(a, b, mob: export_file.ExportMob) -> float:
    """The mob's distance to the piece of line a-b, at 1-yard steps along
    it, as the route library walks it."""
    length = _distance(a, b)
    steps = int(length) + 1
    return min(_to_mob([a[i] + (b[i] - a[i]) * min(s, length) / (length or 1) for i in range(3)], mob)
               for s in range(steps + 1))


def _name(pack: dict) -> str:
    names = ", ".join(m["name"] or str(m["entry"]) for m in pack["mobs"])
    return f"{pack['label'] or pack['key']} ({names})"


def skipped_near(line: list, skipped: list[int], export: export_file.MapExport | None, packs: dict) -> list[dict]:
    """The skipped packs that the route line comes within the aggro range
    of (src/bots/RouteHandOrder.h, SkippedPackWarnings): each with its
    nearest distance and the point that starts the piece of the line where
    it is nearest. A skipped pack can join a fight there as an extra pack.
    The line is the route file's: a skipped pack that it still pulls gets a
    note to build the route again, as .route build measures the new route."""
    mobs = {p.key: p.mobs for p in (export.packs if export else [])}
    points = [((p.x, p.y, p.z), p.name) for p in line]
    near = []
    for key in skipped:
        pack = packs[key]
        pull = f"pull_{pack['label']}"
        if pack["label"] and any(name == pull for _, name in points):
            # The route file is the old route, which pulls the pack. .route
            # build takes it off and measures the new route itself.
            near.append({"key": key, "distance": 0.0, "point": pull,
                         "text": f"the route file still pulls the skipped pack {_name(pack)} at its point {pull}: "
                                 f"run .route build, then read its warnings to see if the new route passes within "
                                 f"its aggro range"})
            continue
        best, by = math.inf, ""
        for i, (at, point) in enumerate(points):
            to = points[min(i + 1, len(points) - 1)][0]
            for mob in mobs.get(key, []):
                # The nearest spot of the piece to the mob, then the mob's distance.
                distance = _to_mob_on_piece(at, to, mob)
                if distance < best:
                    best, by = distance, point
        if best < pack["aggro_radius"]:
            near.append({"key": key, "distance": round(best, 1), "point": by,
                         "text": f"the route passes the skipped pack {_name(pack)} at {best:.0f} yards, within its "
                                 f"aggro range of {pack['aggro_radius']:.0f}, by the point {by}: it can join a fight "
                                 f"as an extra pack"})
    return near


def _is_pack_line(line: str) -> bool:
    return bool(line.split("#", 1)[0].strip())


def _strip_blank(lines: list[str]) -> list[str]:
    while lines and not lines[0].strip():
        lines = lines[1:]
    while lines and not lines[-1].strip():
        lines = lines[:-1]
    return lines


def layout(text: str) -> tuple[list[str], list[tuple[int, str, list[str]]], list[str]]:
    """The hand comments of an order file: its header (the lines above the
    last blank line before the first pack line), each pack line's guid with
    the comment at the end of its line and the comment block above it (the
    lines since the previous pack line, or since the header), and the lines
    after the last pack line."""
    lines = text.splitlines()
    at = [i for i, line in enumerate(lines) if _is_pack_line(line)]
    if not at:
        return _strip_blank(lines), [], []
    leading = lines[:at[0]]
    blanks = [i for i, line in enumerate(leading) if not line.strip()]
    if blanks:
        header, first = leading[:blanks[-1]], leading[blanks[-1] + 1:]
    else:
        header, first = leading, []
    packs = []
    for n, i in enumerate(at):
        block = first if n == 0 else lines[at[n - 1] + 1:i]
        code, _, comment = lines[i].partition("#")
        packs.append((int(code.split()[1]), comment.strip(), _strip_blank(block)))
    return _strip_blank(header), packs, _strip_blank(lines[at[-1] + 1:])


def write(route_name: str, keys: list[int], packs: list[dict], old_text: str | None = None,
          skips: list[int] = ()) -> str:
    """The text of the order file: one `pack <key>` line per pack, then one
    `skip <key>` line per skipped pack, each with its route name and its
    mobs as a comment, or the comment of its old line.
    The hand comments of the old file stay: its header at the top, the comment block above a pack line with
    that pack, the blocks of packs that left the order and the lines after
    the last pack at the end."""
    by_key = {p["key"]: p for p in packs}
    pack_of = {m["guid"]: p["key"] for p in packs for m in p["mobs"]}
    header, old_packs, footer = layout(old_text) if old_text is not None else ([], [], [])
    blocks: dict[int, list[str]] = {}
    comments: dict[int, str] = {}
    given_back: list[list[str]] = []
    for guid, comment, block in old_packs:
        key = pack_of.get(guid)
        if (key in keys or key in skips) and key not in blocks:
            blocks[key] = block
            if comment:
                comments[key] = comment
        elif block:
            given_back.append(block)
    out = list(header or HEADER.format(route=route_name).splitlines())
    lines = [("pack", key) for key in keys] + [("skip", key) for key in skips]
    for n, (word, key) in enumerate(lines):
        pack = by_key[key]
        block = blocks.get(key, [])
        if block:
            out += [""] + block
        elif n == 0 or (word == "skip" and lines[n - 1][0] == "pack"):
            out += [""]
        names = ", ".join(m["name"] or str(m["entry"]) for m in pack["mobs"])
        out.append(f"{word} {key} # {comments.get(key) or (pack['label'] or 'off the route') + ', ' + names}")
    for block in given_back + ([footer] if footer else []):
        out += [""] + block
    return "\n".join(out) + "\n"


class SavedOrder(BaseModel):
    packs: list[int]  # the pack keys of the hand order, in order; a pack off the route is added
    skips: list[int] = []  # the pack keys of the skipped packs


def add_routes(app: FastAPI, maps):
    """The save-order call and the page part. maps is map_web's MapData."""
    @app.get("/pack_order.js")
    def script():
        return FileResponse(HERE / "pack_order.js", media_type="text/javascript")

    @app.post("/api/maps/{name}/order")
    def save(name: str, order: SavedOrder):
        route = maps.route(name)
        packs = maps.packs(route)["packs"]
        known = {p["key"] for p in packs}
        for key in order.packs + order.skips:
            if key not in known:
                raise HTTPException(422, f"{key} is not the key of a pack of the map")
        if len(set(order.packs)) != len(order.packs) or len(set(order.skips)) != len(order.skips):
            raise HTTPException(422, "a pack comes twice in the order")
        if set(order.packs) & set(order.skips):
            raise HTTPException(422, "a pack is both in the order and skipped")
        path = maps.settings.routes / file_name(name)
        old_text = path.read_text() if path.is_file() else None
        if old_text is not None:
            try:
                parse(old_text)
            except PackOrderError as error:
                raise HTTPException(422, f"{path}: {error}")
        path.write_text(write(name, order.packs, packs, old_text, order.skips))
        near = skipped_near(route.line, order.skips, maps.export(route.map_id), {p["key"]: p for p in packs})
        return {"file": path.name, "packs": len(order.packs), "skips": len(order.skips), "skipped_near": near}
