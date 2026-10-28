#!/usr/bin/env python3
"""The dungeon map: a local page that shows the navmesh floor of a dungeon
and its route.

    bots/tools/route-map/route-map <route> [--navmesh DIR] [--settings FILE] [--port 8766] [--no-browser]

The route is a route file name in the routes folder, for example
ragefire_chasm. The map id comes from the route file. The navmesh, routes
and exports folders come from settings.toml; --navmesh overrides the
navmesh folder. The page is http://127.0.0.1:8766/?route=<route>.

The navmesh reader is navmesh.py, the floor image floor_image.py, the route
file reader route_file.py, the page map_page.html. See spec
jvbastel/WOWAI#148 and ADR 0003.
"""

from __future__ import annotations

import argparse
import base64
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse

import export_file
import floor_image
import map_packs
import navmesh
import pack_order
import route_draft
import route_draw
import route_file


HERE = Path(__file__).resolve().parent
PAGE = HERE / "map_page.html"


@dataclass
class Settings:
    navmesh: Path
    routes: Path
    exports: Path

    @classmethod
    def load(cls, path: Path) -> "Settings":
        """The settings file; its relative paths are relative to its folder."""
        paths = tomllib.loads(path.read_text())["paths"]
        return cls(*(path.parent / Path(paths[k]) for k in ("navmesh", "routes", "exports")))


@dataclass
class Command:
    route: str
    settings: Settings
    host: str
    port: int
    browser: bool


def read_command(argv=None) -> Command:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("route", help="the route name, for example ragefire_chasm")
    parser.add_argument("--settings", type=Path, default=HERE / "settings.toml")
    parser.add_argument("--navmesh", type=Path, help="the navmesh folder (default: from the settings file)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--no-browser", dest="browser", action="store_false", help="do not open a browser")
    args = parser.parse_args(argv)
    settings = Settings.load(args.settings)
    if args.navmesh:
        settings.navmesh = args.navmesh
    return Command(args.route, settings, args.host, args.port, args.browser)


def route_path(settings: Settings, name: str) -> Path:
    """The route file of the route name."""
    return settings.routes / f"{name}.txt"


def create_app(settings: Settings) -> FastAPI:
    app = FastAPI(title="Dungeon map")

    def load_route(name):
        path = route_path(settings, name)
        if not path.is_file():
            raise HTTPException(404, f"no route file {path}")
        try:
            return route_file.parse(path.read_text())
        except route_file.RouteFileError as error:
            raise HTTPException(422, f"{path}: {error}")

    @app.get("/", response_class=HTMLResponse)
    def page():
        return PAGE.read_text()

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

    def load_export(map_id):
        try:
            return export_file.load(settings.exports, map_id)
        except export_file.ExportFileError as error:
            raise HTTPException(422, f"export: {error}")

    @app.get("/api/maps/{name}/floors")
    def floors(name: str, x: float, y: float):
        """The floors at (x, y), lowest first."""
        mesh = load_navmesh(load_route(name).map_id)
        return {"floors": [{"z": round(f.z, 2), "lava": f.poly.lava} for f in mesh.floors(x, y)]}

    @app.get("/api/maps/{name}")
    def load_map(name: str, z_min: Optional[float] = None, z_max: Optional[float] = None):
        """The map of a route: the floor image (only heights z_min to z_max
        when given) with its bounds, the route line, every pack of the map (the
        export file) with the route's packs marked, the patrols, the wanderers
        and the bosses."""
        route = load_route(name)
        packs = map_packs.map_part(route, load_export(route.map_id))
        return {
            "route": route.name,
            "map_id": route.map_id,
            "floor": load_floor(route, z_min, z_max),
            "line": [vars(p) for p in route.line],
            **packs,
            **pack_order.map_part(route, packs, settings.routes),
        }

    route_draw.add_routes(app, settings, load_route, load_navmesh)
    map_packs.add_routes(app)
    route_draft.add_routes(app, settings, load_route)
    return app


def open_browser(url):
    """Open the page: in the Windows browser under WSL, else the default one."""
    import shutil
    import subprocess
    import webbrowser

    if shutil.which("explorer.exe"):
        subprocess.Popen(["explorer.exe", url])  # WSL: explorer.exe opens a URL in the Windows browser
    else:
        webbrowser.open(url)


def refuse_continent_route(command: Command):
    """Stop with a message when the route is on a continent map. A route that
    does not parse is left to the page, which shows the error."""
    path = route_path(command.settings, command.route)
    try:
        navmesh.refuse_continent(route_file.parse(path.read_text()).map_id)
    except (OSError, route_file.RouteFileError):
        return
    except navmesh.NavmeshError as error:
        raise SystemExit(f"route-map: {command.route}: {error}")


def main(argv=None):
    command = read_command(argv)
    refuse_continent_route(command)
    import threading

    import uvicorn

    app = create_app(command.settings)
    url = f"http://{command.host}:{command.port}/?route={command.route}"
    print(f"Dungeon map of {command.route}: {url} (navmesh {command.settings.navmesh}). Ctrl+C stops it.")
    if command.browser:
        threading.Timer(1.0, open_browser, [url]).start()
    uvicorn.run(app, host=command.host, port=command.port, log_level="warning")


if __name__ == "__main__":
    main()
