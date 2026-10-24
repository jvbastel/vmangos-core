#!/usr/bin/env python3
"""The icon cache of the template builder page: a one-time script that reads
the item icons from the archives of a local 1.12 client and converts them to
PNG.

    uv run icon_cache.py <client folder> [--out DIR]

For example `uv run icon_cache.py /mnt/c/Games/WoW-1.12.1`. The script only
reads the client. It reads ItemDisplayInfo.dbc for the icon name of each
display ID, then converts each icon (Interface/Icons/<name>.blp) to
<out>/<name lowercase>.png, and writes <out>/display_icons.json (display ID to
icon name). The default out folder is bots/tools/gear/icon-cache/, which git
ignores: the icons belong to Blizzard and stay on this computer.

builder_web.py shows the icons when the cache exists. Without the cache, it
shows the item names in the color of their quality. See step 4 of the
version 1 build spec, "Template builder page".

The MPQ archives are read with mpyq, the BLP files with Pillow.
"""

from __future__ import annotations

import argparse
import io
import json
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_CACHE = HERE / "icon-cache"
INDEX = "display_icons.json"
URL_PREFIX = "/icons"
DISPLAY_INFO = "DBFilesClient\\ItemDisplayInfo.dbc"
ICON_PATH = "Interface\\Icons\\{}.blp"
ICON_FIELD = 5  # ItemDisplayInfo.dbc of 1.12: ID, 2 models, 2 model textures, then the inventory icon.


def read_display_icons(data):
    """The icon name of each display ID in an ItemDisplayInfo.dbc. Rows with
    no icon are left out."""
    magic, count, fields, record_size, string_size = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC" or fields <= ICON_FIELD:
        raise ValueError("not an ItemDisplayInfo.dbc of the 1.12 client")
    strings = data[20 + count * record_size:]
    icons = {}
    for row in range(count):
        record = struct.unpack_from(f"<{fields}I", data, 20 + row * record_size)
        offset = record[ICON_FIELD]
        name = strings[offset:strings.index(b"\0", offset)].decode("utf-8")
        if name:
            icons[record[0]] = name
    return icons


def client_archives(client_dir):
    """The MPQ archives of the client that hold the item display data and the
    icons, in the load order of the client: a later archive wins."""
    data_dir = Path(client_dir) / "Data"
    patches = sorted(data_dir.glob("patch*.MPQ"), key=_patch_number)
    return [data_dir / "dbc.MPQ", data_dir / "interface.MPQ", *patches]


def _patch_number(path):
    match = re.fullmatch(r"patch(?:-(\d+))?\.MPQ", path.name, re.IGNORECASE)
    return int(match.group(1) or 1) if match else 0


def open_archives(paths):
    import mpyq

    return [mpyq.MPQArchive(str(path), listfile=False) for path in paths if path.exists()]


def _read(archives, name):
    """The file from the last archive that has it, or None."""
    for archive in reversed(archives):
        data = archive.read_file(name)
        if data is not None:
            return data
    return None


@dataclass
class Report:
    icons: int = 0
    missing: list[str] = field(default_factory=list)


def build(archives, out_dir):
    """Converts the icon of each display ID to <out_dir>/<name>.png and writes
    the index. archives have read_file(name) -> bytes or None, in load
    order."""
    from PIL import Image

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    data = _read(archives, DISPLAY_INFO)
    if data is None:
        raise FileNotFoundError(f"no {DISPLAY_INFO} in the archives")
    display_icons = read_display_icons(data)
    report = Report()
    found = {}
    for name in display_icons.values():
        key = name.lower()
        if key in found:
            continue
        blp = _read(archives, ICON_PATH.format(name))
        found[key] = blp is not None
        if blp is None:
            report.missing.append(name)
            continue
        Image.open(io.BytesIO(blp)).convert("RGBA").save(out_dir / f"{key}.png")
        report.icons += 1
    index = {str(display_id): name.lower() for display_id, name in display_icons.items() if found[name.lower()]}
    (out_dir / INDEX).write_text(json.dumps(index, sort_keys=True))
    return report


class IconCache:
    """The icon source of the builder page over a cache made by build()."""

    def __init__(self, cache_dir, display_icons):
        self.cache_dir = Path(cache_dir)
        self.display_icons = display_icons

    @classmethod
    def load(cls, cache_dir):
        """The cache in cache_dir, or None when there is no cache."""
        index = Path(cache_dir) / INDEX
        if not index.is_file():
            return None
        display_icons = {int(display_id): name for display_id, name in json.loads(index.read_text()).items()
                         if (index.parent / f"{name}.png").is_file()}
        return cls(cache_dir, display_icons)

    def url(self, item):
        name = self.display_icons.get(item.display_id)
        return f"{URL_PREFIX}/{name}.png" if name else None

    def mount(self, app):
        from fastapi.staticfiles import StaticFiles

        app.mount(URL_PREFIX, StaticFiles(directory=self.cache_dir), name="icons")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("client", type=Path, help="the folder of the 1.12 client (with Data/ in it)")
    parser.add_argument("--out", type=Path, default=DEFAULT_CACHE, help="the cache folder (default: icon-cache)")
    args = parser.parse_args(argv)

    archives = open_archives(client_archives(args.client))
    if not archives:
        print(f"No MPQ archives in {args.client / 'Data'}", file=sys.stderr)
        return 1
    report = build(archives, args.out)
    print(f"{report.icons} icons in {args.out}", file=sys.stderr)
    if report.missing:
        print(f"{len(report.missing)} icons are not in the archives: {', '.join(report.missing)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
