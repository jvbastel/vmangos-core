"""The web API of the route drawing (ticket jvbastel/WOWAI#157): snap a
click to the floor, join two points with a floor path, save the trace file
of the open route. map_web.create_app adds these calls with add_routes; the
page part is map_draw.js.

    GET  /api/maps/{route}/snap?x=&y=[&z=]   the floor point of a click, and the floor heights there
    POST /api/maps/{route}/path              {"start": {x, y, z}, "end": {x, y, z}}: the floor path and its warnings
    POST /api/maps/{route}/trace             {"points": [{x, y, z}, ...]}: writes <route>.trace.txt
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from pydantic import BaseModel

import floor_path
import trace_file

HERE = Path(__file__).resolve().parent


class Spot(BaseModel):
    x: float
    y: float
    z: float


class PathAsk(BaseModel):
    start: Spot
    end: Spot


class TraceAsk(BaseModel):
    points: list[Spot]


def _vec(spot: Spot):
    return (spot.x, spot.y, spot.z)


def _xyz(point):
    return {"x": round(point[0], 2), "y": round(point[1], 2), "z": round(point[2], 2)}


def piece_view(piece: floor_path.Piece):
    return {"points": [_xyz(p) for p in piece.points], "length": round(piece.length, 1),
            "straight": round(piece.straight, 1), "warnings": piece.warnings}


def add_routes(app: FastAPI, settings, load_route, load_navmesh):
    graphs = {}

    def graph(map_id):
        if map_id not in graphs:
            graphs[map_id] = floor_path.FloorGraph(load_navmesh(map_id))
        return graphs[map_id]

    @app.get("/map_draw.js")
    def script():
        return FileResponse(HERE / "map_draw.js", media_type="text/javascript")

    @app.get("/api/maps/{name}/snap")
    def snap(name: str, x: float, y: float, z: Optional[float] = None):
        point, floors = graph(load_route(name).map_id).snap(x, y, z)
        if point is None:
            raise HTTPException(422, "no floor on this map")
        return {"point": _xyz(point), "floors": [round(h, 2) for h in floors]}

    @app.post("/api/maps/{name}/path")
    def path(name: str, ask: PathAsk):
        return piece_view(graph(load_route(name).map_id).path(_vec(ask.start), _vec(ask.end)))

    @app.post("/api/maps/{name}/trace")
    def save_trace(name: str, ask: TraceAsk):
        """Writes the trace file of the route from the drawn points: the
        floor path of each piece, as points a few yards apart."""
        route = load_route(name)
        floors = graph(route.map_id)
        clicks = [_vec(s) for s in ask.points]
        points = clicks[:1]
        warnings = []
        for k, (a, b) in enumerate(zip(clicks, clicks[1:]), 1):
            piece = floors.path(a, b)
            points += piece.points[1:]
            warnings += [{**w, "piece": k} for w in piece.warnings]
        path = settings.routes / f"{name}.trace.txt"
        path.write_text(trace_file.write(route.name, route.map_id, points))
        return {"file": str(path), "points": len(points), "warnings": warnings}
