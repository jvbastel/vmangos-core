"""The web API of the route drawing (ticket jvbastel/WOWAI#157): snap a
click to the floor, join two points with a floor path, save the trace file
of the open route. map_web.create_app adds these calls with add_routes; the
page part is map_draw.js.

    GET  /api/maps/{route}/snap?x=&y=[&z=][&through=<region>...]
                                             the floor point of a click, and the floor heights there; the ground
                                             of the regions named in through (faded or hidden) is left out (#185)
    POST /api/maps/{route}/path              {"start": {x, y, z}, "end": {x, y, z}, "through": [names]}: the floor
                                             path and its warnings; the path may run through any region
    POST /api/maps/{route}/trace             {"points": [{x, y, z, joined, mark, note}, ...], "branches": [[...], ...]}:
                                             writes <route>.trace.txt (each branch is a later walk)
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse
from pydantic import BaseModel

import floor_path
import regions
import route_draft
import trace_file

HERE = Path(__file__).resolve().parent


class Spot(BaseModel):
    x: float
    y: float
    z: float


class PathAsk(BaseModel):
    start: Spot
    end: Spot
    through: list[str] = []  # the faded or hidden regions (#185): a path may still run through them


class TraceAsk(BaseModel):
    points: list[route_draft.DraftPoint]
    branches: list[list[route_draft.DraftPoint]] = []  # later walks, out and back from the main line (#158)


def _vec(spot: Spot):
    return (spot.x, spot.y, spot.z)


def _xyz(point):
    return {"x": round(point[0], 2), "y": round(point[1], 2), "z": round(point[2], 2)}


def piece_view(piece: floor_path.Piece):
    return {"points": [_xyz(p) for p in piece.points], "length": round(piece.length, 1),
            "straight": round(piece.straight, 1), "warnings": piece.warnings}


def add_routes(app: FastAPI, maps):
    """The draw calls and the page part. maps is map_web's MapData."""
    graphs = {}

    def graph(map_id):
        if map_id not in graphs:
            graphs[map_id] = floor_path.FloorGraph(maps.navmesh(map_id))
        return graphs[map_id]

    @app.get("/map_draw.js")
    def script():
        return FileResponse(HERE / "map_draw.js", media_type="text/javascript")

    @app.get("/api/maps/{name}/snap")
    def snap(name: str, x: float, y: float, z: Optional[float] = None, through: list[str] = Query([])):
        map_id = maps.route(name).map_id
        passed = [r for r in maps.regions(map_id) if r.name in through]
        point, floors = graph(map_id).snap(x, y, z, through=lambda p: any(r.holds(*p) for r in passed))
        if point is None:
            raise HTTPException(422, "no floor on this map")
        return {"point": regions.mark(_xyz(point), maps.regions(map_id)),  # the regions it is in (#184)
                "floors": [round(h, 2) for h in floors]}

    @app.post("/api/maps/{name}/path")
    def path(name: str, ask: PathAsk):
        return piece_view(graph(maps.route(name).map_id).path(_vec(ask.start), _vec(ask.end)))

    @app.post("/api/maps/{name}/trace")
    def save_trace(name: str, ask: TraceAsk):
        """Writes the trace file of the route from the drawn points: the
        floor path of each piece, as points a few yards apart."""
        route = maps.route(name)
        trace, warnings = route_draft.draft_trace(route.name, route.map_id, ask.points, ask.branches,
                                                  graph(route.map_id).path)
        path = maps.settings.routes / f"{name}.trace.txt"
        part = path.with_suffix(".txt.part")  # write, then rename: a second save never reads half a file
        part.write_text(trace_file.write_trace(trace))
        part.replace(path)
        return {"file": str(path), "points": len(trace.points), "warnings": warnings}
