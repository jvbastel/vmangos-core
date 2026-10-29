"""The walked path of a run trace (jvbastel/WOWAI#152): where each group
member really walked in a scripted run, for a layer of the dungeon map.

A scripted full run writes the trace of the whole run beside its result
(<stem>.trace.txt in Bots.Run.ResultDir; runs.py copies it). Each move of a
member is a line `<run time> <member> at <x> <y> <z>` (src/bots/RunTrace.h).
The other lines (targets, casts, chat, an enemy that flees) are left out.
"""

from __future__ import annotations

import re
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse

import regions

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "walked_path.js"

NUMBER = r"(-?\d+(?:\.\d+)?)"
# A member name is one word; an enemy line ("<name> <guid> flees at ...")
# has more words and does not match.
POSITION = re.compile(rf"^{NUMBER} (\S+) at {NUMBER} {NUMBER} {NUMBER}$")


def read(text: str) -> list[dict]:
    """The walked path of each member, in the order the trace first names
    them: [{"name", "points": [{"t", "x", "y", "z"}]}]."""
    members: dict[str, list[dict]] = {}
    for line in text.splitlines():
        found = POSITION.match(line.strip())
        if found:
            t, name, x, y, z = found.groups()
            members.setdefault(name, []).append({"t": float(t), "x": float(x), "y": float(y), "z": float(z)})
    return [{"name": name, "points": points} for name, points in members.items()]


def add_routes(app: FastAPI, maps) -> None:
    """The walked path call and the page's layer script. maps is map_web's
    MapData: each position carries the regions it is in (regions.mark)."""

    @app.get("/api/maps/{name}/walked")
    def walked(name: str, trace: str):
        """The walked path of the run trace file `trace` (a path on this
        machine), drawn over the map of route `name`."""
        path = Path(trace).expanduser()
        if not path.is_file():
            raise HTTPException(404, f"no run trace {path}")
        members = read(path.read_text(errors="replace"))
        if not members:
            raise HTTPException(422, f"{path}: no member positions (`<time> <member> at <x> <y> <z>` lines)")
        return regions.mark({"trace": path.name, "members": members}, maps.regions(maps.route(name).map_id))

    @app.get("/walked_path.js")
    def script():
        return FileResponse(SCRIPT, media_type="text/javascript")
