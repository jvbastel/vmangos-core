import io
import struct
from types import SimpleNamespace

from fastapi.testclient import TestClient
from PIL import Image

import builder_web
import gear_data
import icon_cache
from fixtures import item, tables
from test_generate import settings

ICON_FIELD = 5


def dbc(rows):
    """An ItemDisplayInfo.dbc with 23 fields per row; rows maps a display ID
    to an icon name."""
    strings = b"\0"
    records = b""
    for display_id, icon in rows.items():
        fields = [0] * 23
        fields[0] = display_id
        fields[ICON_FIELD] = len(strings)
        strings += icon.encode() + b"\0"
        records += struct.pack("<23I", *fields)
    return struct.pack("<4s4I", b"WDBC", len(rows), 23, 23 * 4, len(strings)) + records + strings


def blp(color):
    image = Image.new("P", (4, 4))
    image.putpalette(list(color) * 256)
    data = io.BytesIO()
    image.save(data, "BLP")
    return data.getvalue()


class Archive:
    """An MPQ archive: read_file(name) gives the bytes or None. MPQ names
    are not case-sensitive."""

    def __init__(self, files):
        self.files = {name.lower(): data for name, data in files.items()}

    def read_file(self, name):
        return self.files.get(name.lower())


def test_the_display_info_gives_the_icon_of_each_display_id():
    assert icon_cache.read_display_icons(dbc({220: "INV_Robe_02", 222: "", 224: "INV_Jewelry_Ring_03"})) == {
        220: "INV_Robe_02", 224: "INV_Jewelry_Ring_03"}


def test_the_script_converts_the_icons_to_png_and_a_later_archive_wins(tmp_path):
    base = Archive({"DBFilesClient\\ItemDisplayInfo.dbc": dbc({1: "INV_Robe_02", 2: "INV_Missing"}),
                    "Interface\\Icons\\INV_Robe_02.blp": blp((255, 0, 0))})
    patch = Archive({"DBFilesClient\\ItemDisplayInfo.dbc": dbc({1: "INV_Robe_02", 3: "inv_robe_02"}),
                     "INTERFACE\\ICONS\\inv_robe_02.BLP": blp((0, 255, 0))})

    report = icon_cache.build([base, patch], tmp_path)

    png = Image.open(tmp_path / "inv_robe_02.png")
    assert png.format == "PNG"
    assert png.convert("RGB").getpixel((0, 0)) == (0, 255, 0)
    assert (report.icons, report.missing) == (1, [])
    cache = icon_cache.IconCache.load(tmp_path)
    assert cache.url(SimpleNamespace(display_id=1)) == "/icons/inv_robe_02.png"
    assert cache.url(SimpleNamespace(display_id=3)) == "/icons/inv_robe_02.png"
    assert cache.url(SimpleNamespace(display_id=2)) is None


def test_an_icon_missing_from_the_archives_is_reported(tmp_path):
    archive = Archive({"DBFilesClient\\ItemDisplayInfo.dbc": dbc({2: "INV_Missing"})})

    report = icon_cache.build([archive], tmp_path)

    assert (report.icons, report.missing) == (0, ["INV_Missing"])


def test_without_a_cache_there_is_no_icon_source(tmp_path):
    assert icon_cache.IconCache.load(tmp_path / "icon-cache") is None
    assert icon_cache.IconCache.load(tmp_path) is None


def test_the_page_serves_the_icons_of_the_cache(tmp_path):
    cache_dir = tmp_path / "icon-cache"
    icon_cache.build([Archive({"DBFilesClient\\ItemDisplayInfo.dbc": dbc({7012: "INV_Robe_02"}),
                               "Interface\\Icons\\INV_Robe_02.blp": blp((255, 0, 0))})], cache_dir)
    world = gear_data.build_world(tables(items=[item(100, display_id=7012), item(101)]))
    web = TestClient(builder_web.create_app(world, tmp_path, settings(),
                                            icons=icon_cache.IconCache.load(cache_dir)))

    found = web.get("/api/search", params={"slot": "chest"}).json()["items"]
    icon = web.get("/icons/inv_robe_02.png")

    assert {i["entry"]: i["icon"] for i in found} == {100: "/icons/inv_robe_02.png", 101: None}
    assert icon.status_code == 200
    assert icon.headers["content-type"] == "image/png"


def test_the_client_archives_load_in_the_order_of_the_client(tmp_path):
    (tmp_path / "Data").mkdir()
    for name in ["patch-2.MPQ", "dbc.MPQ", "patch.MPQ", "patch-10.MPQ", "interface.MPQ", "base.MPQ", "speech.MPQ"]:
        (tmp_path / "Data" / name).write_bytes(b"")

    assert [path.name for path in icon_cache.client_archives(tmp_path)] == [
        "dbc.MPQ", "interface.MPQ", "patch.MPQ", "patch-2.MPQ", "patch-10.MPQ"]


def test_the_page_takes_the_cache_when_it_exists_and_no_icons_otherwise(tmp_path):
    icon_cache.build([Archive({"DBFilesClient\\ItemDisplayInfo.dbc": dbc({7012: "INV_Robe_02"}),
                               "Interface\\Icons\\INV_Robe_02.blp": blp((255, 0, 0))})], tmp_path / "cache")

    assert isinstance(builder_web.icon_source(tmp_path / "cache"), icon_cache.IconCache)
    assert isinstance(builder_web.icon_source(tmp_path / "none"), builder_web.NoIcons)
