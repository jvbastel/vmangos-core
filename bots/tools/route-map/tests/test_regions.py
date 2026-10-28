"""The player outlines regions of stacked ground on the dungeon map and saves
them with the dungeon, through the load-regions and save-regions calls
(ticket jvbastel/WOWAI#182, spec #179)."""

import pytest
from fastapi.testclient import TestClient

import map_web
from draw_fixtures import BORDER_X, CAVE, ISLAND
from fixtures import write_route, write_tile

ROUTE = "route drawn_cave 389\npoint p1 5.00 5.00 0.00 2.00\n"
# The ledge over L1 (z 8) and the floor under it (z 0), side by side.
LEDGE = {"name": "Ledge", "low": 3.0, "high": 13.0, "outline": [[0.0, -10.0], [10.0, -10.0], [10.0, 10.0], [0.0, 10.0]]}
UNDER = {"name": "Region 2", "low": -5.0, "high": 5.0, "outline": [[0.0, 0.0], [10.0, 0.0], [10.0, 10.0]]}


@pytest.fixture
def routes(tmp_path):
    return tmp_path / "routes"


@pytest.fixture
def web(tmp_path, routes):
    write_tile(tmp_path / "mmaps", 389, polys=CAVE, next_tile=ISLAND, border_x=BORDER_X)
    write_route(routes, "drawn_cave", ROUTE)
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=routes, exports=routes)
    return TestClient(map_web.create_app(settings))


def load(web):
    response = web.get("/api/maps/drawn_cave/regions")
    assert response.status_code == 200, response.text
    return response.json()


def save(web, *regions):
    return web.post("/api/maps/drawn_cave/regions", json={"regions": list(regions)})


def test_a_save_writes_the_regions_file_of_the_map_and_a_load_gives_them_back(web, routes):
    response = save(web, LEDGE, UNDER)

    assert response.status_code == 200, response.text
    assert (routes / "map_389.regions.txt").is_file()
    loaded = load(web)
    assert loaded["regions"] == [LEDGE, UNDER]
    assert (loaded["file"], loaded["exists"]) == ("map_389.regions.txt", True)


def test_a_saved_file_reloads_unchanged(web, routes):
    save(web, LEDGE, UNDER)
    text = (routes / "map_389.regions.txt").read_text()

    save(web, *load(web)["regions"])

    assert (routes / "map_389.regions.txt").read_text() == text


HAND_NOTES = """\
# My regions of the cave.

# The ledge over the first room.
region 3.00 13.00 0.00 -10.00 10.00 -10.00 10.00 10.00 0.00 10.00 name Ledge  # drawn by hand
region -5.00 5.00 0.00 0.00 10.00 0.00 10.00 10.00 name Region 2

# A note at the end.
"""


def test_a_save_keeps_the_comment_lines_of_the_file(web, routes):
    (routes / "map_389.regions.txt").write_text(HAND_NOTES)
    loaded = load(web)["regions"]
    assert loaded == [LEDGE, UNDER]

    response = save(web, *loaded)

    assert response.status_code == 200, response.text
    assert (routes / "map_389.regions.txt").read_text() == HAND_NOTES


def test_the_comments_above_a_region_go_with_it_and_those_of_a_deleted_region_stay(web, routes):
    (routes / "map_389.regions.txt").write_text(HAND_NOTES)

    save(web, UNDER, {**LEDGE, "high": 12.0})
    moved = (routes / "map_389.regions.txt").read_text()
    save(web, UNDER)
    deleted = (routes / "map_389.regions.txt").read_text()

    assert moved == """\
# My regions of the cave.

region -5.00 5.00 0.00 0.00 10.00 0.00 10.00 10.00 name Region 2

# The ledge over the first room.
region 3.00 12.00 0.00 -10.00 10.00 -10.00 10.00 10.00 0.00 10.00 name Ledge  # drawn by hand

# A note at the end.
"""
    assert deleted == """\
# My regions of the cave.

region -5.00 5.00 0.00 0.00 10.00 0.00 10.00 10.00 name Region 2

# The ledge over the first room.

# A note at the end.
"""
