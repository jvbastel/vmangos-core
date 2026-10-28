#!/usr/bin/env python3
"""The dungeon map: a local page that shows the navmesh floor of a dungeon
and its route.

    uv run map_web.py <route> [--navmesh DIR] [--settings FILE] [--port 8766]

See spec jvbastel/WOWAI#148 and ADR 0003.
"""

from __future__ import annotations

import base64
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException

import floor_image
import navmesh
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

    meshes = {}

    def load_navmesh(map_id):
        if map_id not in meshes:
            try:
                meshes[map_id] = navmesh.Navmesh.load(settings.navmesh, map_id)
            except navmesh.NavmeshError as error:
                raise HTTPException(422, f"navmesh: {error}")
        return meshes[map_id]

    images = {}

    def load_floor(route, z_min, z_max):
        key = (route.map_id, tuple((p.x, p.y, p.z) for p in route.line + route.packs), z_min, z_max)
        if key not in images:
            mesh = load_navmesh(route.map_id)
            polys = mesh.joined_to([(p.x, p.y, p.z) for p in route.line + route.packs])
            try:
                image = floor_image.render(polys, z_min, z_max)
            except floor_image.EmptyFloor as error:
                raise HTTPException(422, str(error))
            images[key] = {"image": "data:image/png;base64," + base64.b64encode(image.png).decode(),
                           "bounds": image.bounds}
        return images[key]

    @app.get("/api/maps/{name}/floors")
    def floors(name: str, x: float, y: float):
        """The floors at (x, y), lowest first."""
        mesh = load_navmesh(load_route(name).map_id)
        return {"floors": [{"z": round(f.z, 2), "lava": f.poly.lava} for f in mesh.floors(x, y)]}

    @app.get("/api/maps/{name}")
    def load_map(name: str, z_min: Optional[float] = None, z_max: Optional[float] = None):
        """The map of a route: the floor image (only heights z_min to z_max
        when given) with its bounds, the route line, and the route's packs."""
        route = load_route(name)
        return {
            "route": route.name,
            "map_id": route.map_id,
            "floor": load_floor(route, z_min, z_max),
            "line": [vars(p) for p in route.line],
            "packs": [pack_view(route, pack, order) for order, pack in enumerate(route.packs, 1)],
        }

    return app
