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
import math
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse, HTMLResponse

import export_file
import floor_image
import map_packs
import map_quests
import navmesh
import pack_order
import regions
import route_draft
import route_draw
import route_file
import walked_path


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


class MapData:
    """The loaders of the calls, shared by every part: the route file, the
    navmesh, the export and the packs of a route's map. A navmesh is read
    once per map."""

    def __init__(self, settings: Settings):
        self.settings = settings
        self._meshes: dict = {}

    def route(self, name: str) -> route_file.Route:
        path = route_path(self.settings, name)
        if not path.is_file():
            raise HTTPException(404, f"no route file {path}")
        try:
            return route_file.parse(path.read_text())
        except route_file.RouteFileError as error:
            raise HTTPException(422, f"{path}: {error}")

    def navmesh(self, map_id: int) -> navmesh.Navmesh:
        if map_id not in self._meshes:
            try:
                self._meshes[map_id] = navmesh.Navmesh.load(self.settings.navmesh, map_id)
            except navmesh.NavmeshError as error:
                raise HTTPException(422, f"navmesh: {error}")
        return self._meshes[map_id]

    def export(self, map_id: int) -> Optional[export_file.MapExport]:
        try:
            return export_file.load(self.settings.exports, map_id)
        except export_file.ExportFileError as error:
            raise HTTPException(422, f"export: {error}")

    def regions(self, map_id: int) -> list[regions.Region]:
        """The regions of the map (regions.py), none with no regions file."""
        try:
            return regions.load(self.settings.routes, map_id)
        except regions.RegionsError as error:
            raise HTTPException(422, f"regions: {error}")

    def packs(self, route: route_file.Route) -> dict:
        """map_packs' part of the route's map: one pack per key."""
        return map_packs.map_part(route, self.export(route.map_id))


def join_parts(*parts: dict) -> dict:
    """The parts of the load-map result in one dict; a key in two parts is an error."""
    joined: dict = {}
    for part in parts:
        shared = joined.keys() & part.keys()
        if shared:
            raise ValueError(f"load-map parts share the keys {sorted(shared)}")
        joined.update(part)
    return joined


def mark_regions(data: dict, map_regions: list[regions.Region]) -> dict:
    """The load-map result with region marks (regions.mark): every position
    carries the regions it is in, and a quest badge those of its pack."""
    if not map_regions:
        return data
    data = regions.mark(data, map_regions)
    pack_regions = {p["key"]: p["regions"] for p in data["packs"]}
    data["quest_badges"] = [{**b, "regions": pack_regions.get(b["pack"], [])} for b in data["quest_badges"]]
    return data


def create_app(settings: Settings) -> FastAPI:
    app = FastAPI(title="Dungeon map")
    maps = MapData(settings)

    @app.get("/", response_class=HTMLResponse)
    def page():
        return PAGE.read_text()

    @app.get("/map_common.js")
    def common_script():
        """The shared helpers of the page's scripts: SVG, el, sx/sy, toMap, sizeMarks."""
        return FileResponse(HERE / "map_common.js", media_type="text/javascript")

    images = {}

    def data_url(png: bytes) -> str:
        return "data:image/png;base64," + base64.b64encode(png).decode()

    def load_floor(route, map_regions):
        """The floor of the route's map: one image of all the ground, or with
        regions, one ground image per region and one of the ground in no
        region (floor_parts)."""
        key = (route.map_id, tuple((p.x, p.y, p.z) for p in route.line + route.packs), repr(map_regions))
        if key not in images:
            mesh = maps.navmesh(route.map_id)
            polys = mesh.joined_to([(p.x, p.y, p.z) for p in route.line + route.packs])
            try:
                if not map_regions:
                    image = floor_image.render(polys)
                    images[key] = {"image": data_url(image.png), "bounds": image.bounds}
                else:
                    images[key] = floor_parts(polys, map_regions)
            except floor_image.EmptyFloor as error:
                raise HTTPException(422, str(error))
        return images[key]

    def floor_parts(polys, map_regions):
        """The ground images, lowest first (the mean height of their ground),
        each {"region": its name, or None for the ground in no region,
        "image"}, all with the bounds of the whole floor. A poly is in a
        region when its middle is: the mean of its points. A poly in two
        regions (at a border) is in both images."""
        names = [None] + [r.name for r in map_regions]
        parts: dict = {name: [] for name in names}
        for poly in polys:
            n = len(poly.points)
            mid = (sum(q[0] for q in poly.points) / n, sum(q[1] for q in poly.points) / n, poly.mean_z)
            for name in regions.regions_at(map_regions, *mid) or [None]:
                parts[name].append(poly)
        bounds, pngs = floor_image.render_parts(polys, [parts[name] for name in names])

        def height(name):
            part = parts[name]
            return sum(p.mean_z for p in part) / len(part) if part else -math.inf

        layers = sorted(zip(names, pngs), key=lambda layer: height(layer[0]))
        return {"bounds": bounds, "images": [{"region": name, "image": data_url(png)} for name, png in layers]}

    @app.get("/api/maps/{name}/floors")
    def floors(name: str, x: float, y: float):
        """The floors at (x, y), lowest first."""
        mesh = maps.navmesh(maps.route(name).map_id)
        return {"floors": [{"z": round(f.z, 2), "lava": f.poly.lava} for f in mesh.floors(x, y)]}

    @app.get("/api/maps/{name}")
    def load_map(name: str):
        """The map of a route: the floor (load_floor) with its bounds, the
        route line, every pack of the map (the
        export file) with the route's packs marked, the pack order, the
        patrols, the wanderers, the bosses, and the quest objectives with what
        the built route meets."""
        route = maps.route(name)
        export = maps.export(route.map_id)
        packs = map_packs.map_part(route, export)
        order = pack_order.map_part(name, packs["packs"], settings.routes, route.line, export)
        quests = map_quests.map_part(route, export, packs["packs"], order["pack_order"]["pulled"])
        map_regions = maps.regions(route.map_id)
        base = {"route": route.name, "map_id": route.map_id, "floor": load_floor(route, map_regions),
                "line": [vars(p) for p in route.line]}
        return mark_regions(join_parts(base, packs, order, quests), map_regions)

    walked_path.add_routes(app, maps)
    route_draw.add_routes(app, maps)
    map_packs.add_routes(app)
    pack_order.add_routes(app, maps)
    map_quests.add_routes(app)
    route_draft.add_routes(app, maps)
    regions.add_routes(app, maps)
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
