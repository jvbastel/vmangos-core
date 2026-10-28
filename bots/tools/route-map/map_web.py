#!/usr/bin/env python3
"""The dungeon map: a local page that shows the navmesh floor of a dungeon
and its route.

    uv run map_web.py <route> [--navmesh DIR] [--settings FILE] [--port 8766]

See spec jvbastel/WOWAI#148 and ADR 0003.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from fastapi import FastAPI, HTTPException

import route_file


@dataclass
class Settings:
    navmesh: Path
    routes: Path
    exports: Path


def pack_view(route, pack, order):
    """A pack of the route: its `packN` label, its place in the pack order
    (the order of the route file), its key, mobs and pull point."""
    pull = route.pull_point(pack)
    return {"label": pack.name, "order": order, "key": pack.key, "x": pack.x, "y": pack.y, "z": pack.z,
            "mobs": [vars(m) for m in pack.mobs], "pull_point": vars(pull) if pull else None}


def create_app(settings: Settings) -> FastAPI:
    app = FastAPI(title="Dungeon map")

    def load_route(name):
        path = settings.routes / f"{name}.txt"
        if not path.is_file():
            raise HTTPException(404, f"no route file {path}")
        try:
            return route_file.parse(path.read_text())
        except route_file.RouteFileError as error:
            raise HTTPException(422, f"{path}: {error}")

    @app.get("/api/maps/{name}")
    def load_map(name: str):
        route = load_route(name)
        return {
            "route": route.name,
            "map_id": route.map_id,
            "line": [vars(p) for p in route.line],
            "packs": [pack_view(route, pack, order) for order, pack in enumerate(route.packs, 1)],
        }

    return app
