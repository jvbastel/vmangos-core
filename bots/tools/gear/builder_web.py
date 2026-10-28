#!/usr/bin/env python3
"""The template builder page: a small FastAPI page that builds hand-made
gear templates outside the client.

    uv run builder_web.py [--port 8765] [--gear-dir DIR]

It reads the world database once at the start, with the same environment
variables as generate.py (WOWAI_GEAR_DB_HOST and the rest). The script
builder-from-realm makes the read-only user in a test realm and runs this
page with it. Then open http://127.0.0.1:8765/.

The page has a search per slot, a paste box for item IDs and Wowhead links,
and the slots of the template with their empty levels. It writes only
hand-made templates, to bots/data/gear/<name>.txt. The logic is in
builder.py, the item data in gear_data.py (shared with the generator). The
page shows item icons when icon_cache.py has made the icon cache. See
step 4 of the version 1 build spec, "Template builder page".
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

import builder
import gear_data
import generate
import icon_cache

HERE = Path(__file__).resolve().parent
PAGE = HERE / "builder_page.html"
PATCH_NAMES = {patch: f"1.{patch + 2}" for patch in gear_data.PATCHES}
SOURCE_KINDS = ["quest", "creature", "world", "dungeon", "raid", "vendor", "craft", "none"]


class NoIcons:
    """The icon source with no icons: the page shows the item names in the
    color of their quality. An icon source has url(item) -> str or None, and
    may have mount(app) to serve its files."""

    def url(self, item):
        return None


def icon_source(cache_dir):
    """The icons of the cache in cache_dir (made by icon_cache.py), or no
    icons when there is no cache."""
    return icon_cache.IconCache.load(cache_dir) or NoIcons()


class LineModel(BaseModel):
    slot: str
    entry: int
    min_level: int
    max_level: int
    min_patch: int
    factions: str = "both"   # kept as read: the page has no faction field


class TemplateModel(BaseModel):
    name: str
    class_name: str
    role: str
    items: list[LineModel] = []

    def to_template(self):
        return builder.HandTemplate(self.name, self.class_name, self.role,
                                    [builder.HandItem(**line.model_dump()) for line in self.items])

    @staticmethod
    def of(template):
        return {"name": template.name, "class_name": template.class_name, "role": template.role,
                "items": [vars(line) for line in template.items]}


class PasteModel(BaseModel):
    text: str


def create_app(world, gear_dir, settings, bans=frozenset(), icons=None):
    """The page over the item data of world. gear_dir is the folder of the
    template files, settings the generator settings (for the stat weights of
    each role)."""
    icons = icons or NoIcons()
    gear_dir = Path(gear_dir)
    app = FastAPI(title="WOWAI template builder")
    if hasattr(icons, "mount"):
        icons.mount(app)

    def weights(class_name, role):
        role_settings = settings.roles.get(f"{class_name}_{role}")
        return role_settings.weights if role_settings else {}

    def source_view(source, patch):
        levels = [level for level in source.levels if level is not None]
        level = source.levels[patch] if patch is not None else min(levels)
        return {"kind": source.kind, "name": source.name, "level": level}

    def item_view(item, score=0.0, sources=None, patch=None, class_name=None, slot=None):
        if sources is None:
            sources = builder.sources_in(world, item.entry, patch)
        return {
            "entry": item.entry, "name": item.name, "quality": item.quality, "quality_name": item.quality_name,
            "slot": item.slot, "required_level": item.required_level, "armor_type": item.armor_type,
            "weapon_type": item.weapon_type, "stats": item.stats, "armor": item.armor, "dps": round(item.dps, 1),
            "score": score, "sources": [source_view(source, patch) for source in sources],
            "banned": item.entry in bans, "excluded": item.excluded, "patches": sorted(item.patches),
            "icon": icons.url(item),
            "usable": builder.class_can_wear(item, slot or builder.first_slot(item), class_name) if class_name else True,
        }

    def item_views(entries, class_name=None, role=None):
        result = {}
        for entry in entries:
            item = world.items.get(entry)
            if item:
                score = round(gear_data.score(item, weights(class_name, role)), 2)
                result[entry] = item_view(item, score, class_name=class_name)
        return result

    @app.get("/", response_class=HTMLResponse)
    def page():
        return PAGE.read_text()

    @app.get("/api/meta")
    def meta():
        return {
            "slots": builder.SLOTS, "positions": builder.POSITIONS, "classes": builder.CLASS_ROLES,
            "qualities": gear_data.QUALITY_NAMES, "source_kinds": SOURCE_KINDS,
            "armor_types": sorted(set(gear_data.ARMOR_TYPES.values())), "patches": PATCH_NAMES,
            "levels": builder.LEVELS, "templates": builder.hand_made_names(gear_dir),
            "weighted_roles": sorted(settings.roles),
        }

    @app.get("/api/search")
    def search(slot: str, class_name: Optional[str] = None, role: Optional[str] = None,
               level_min: Optional[int] = None, level_max: Optional[int] = None,
               quality: list[int] = Query([]), source: list[str] = Query([]), patch: Optional[int] = None,
               armor: list[str] = Query([]), text: str = "", usable_only: bool = True, limit: int = 200):
        """class_name and role choose the stat weights of the score. With
        usable_only, the search keeps only what the class can use."""
        if slot not in builder.SLOTS:
            raise HTTPException(400, f"no slot {slot}")
        if class_name is not None and class_name not in builder.CLASS_ROLES:
            raise HTTPException(400, f"no class {class_name}")
        filters = builder.Filters(class_name=class_name if usable_only else None,
                                  level_min=level_min, level_max=level_max,
                                  qualities=frozenset(quality), source_kinds=frozenset(source), patch=patch,
                                  armor_types=frozenset(armor), text=text)
        found = builder.search(world, slot, filters, weights(class_name, role), bans, limit)
        return {"items": [item_view(f.item, f.score, f.sources, patch, class_name, slot) for f in found]}

    @app.post("/api/paste")
    def paste(body: PasteModel):
        results = []
        for checked in builder.check_paste(builder.read_paste(body.text), world):
            result = {"entry": checked.entry, "ok": checked.ok, "problem": checked.problem}
            if checked.ok:
                result["item"] = item_view(checked.item)
                result["line"] = vars(builder.new_line(checked.item))
            results.append(result)
        return {"results": results}

    @app.post("/api/check")
    def check(body: TemplateModel):
        template = body.to_template()
        return {
            "problems": builder.check(template, world),
            "empty": builder.empty_levels(template),
            "items": item_views({line.entry for line in template.items}, template.class_name, template.role),
        }

    @app.get("/api/templates/{name}")
    def load(name: str):
        if name not in builder.hand_made_names(gear_dir):
            raise HTTPException(404, f"no hand-made template {name}")
        template = builder.parse((gear_dir / f"{name}.txt").read_text())
        return {"template": TemplateModel.of(template)}

    @app.put("/api/templates/{name}")
    def save(name: str, body: TemplateModel):
        if body.name != name:
            raise HTTPException(400, "the name in the path and in the template differ")
        try:
            path = builder.save(gear_dir, body.to_template(), world)
        except builder.SaveError as error:
            raise HTTPException(400, str(error))
        return {"path": str(path)}

    return app


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--gear-dir", type=Path, default=generate.DEFAULT_OUT,
                        help="the gear folder (default: bots/data/gear)")
    parser.add_argument("--settings", type=Path, default=HERE / "settings.toml")
    parser.add_argument("--bans", type=Path, default=HERE / "bans.txt")
    parser.add_argument("--icons", type=Path, default=icon_cache.DEFAULT_CACHE,
                        help="the icon cache of icon_cache.py (default: icon-cache)")
    args = parser.parse_args(argv)

    import uvicorn

    settings = generate.Settings.load(args.settings)
    icons = icon_source(args.icons)
    if isinstance(icons, NoIcons):
        print(f"No icon cache in {args.icons}: the page shows item names by quality", file=sys.stderr)
    bans = gear_data.read_bans(args.bans)
    env = os.environ
    connection = gear_data.connect(env.get("WOWAI_GEAR_DB_HOST", "127.0.0.1"), int(env.get("WOWAI_GEAR_DB_PORT", "3306")),
                                   env.get("WOWAI_GEAR_DB_USER", "wowai_gear"), env.get("WOWAI_GEAR_DB_PASSWORD", ""),
                                   env.get("WOWAI_GEAR_DB_NAME", "mangos"))
    try:
        world = gear_data.load_world(connection)
    finally:
        connection.close()
    print(f"Open http://{args.host}:{args.port}/", file=sys.stderr)
    uvicorn.run(create_app(world, args.gear_dir, settings, bans, icons), host=args.host, port=args.port)
    return 0


if __name__ == "__main__":
    sys.exit(main())
