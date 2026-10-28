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


GOOD = "region 3.00 13.00 0.00 -10.00 10.00 -10.00 10.00 10.00 name Ledge\n"


@pytest.mark.parametrize("regions, error", [
    ([{**UNDER, "outline": [[0.0, 0.0], [10.0, 0.0]]}],
     "not saved: region 'Region 2': the outline has 2 points; it needs at least 3"),
    ([{**UNDER, "low": 5.0}], "not saved: region 'Region 2': the low height 5 is not below the high height 5"),
    ([{**UNDER, "low": 6.0}], "not saved: region 'Region 2': the low height 6 is not below the high height 5"),
    ([LEDGE, {**UNDER, "name": "Ledge"}], "not saved: region 'Ledge': two regions have this name"),
    ([{**UNDER, "name": " "}], "not saved: a region has no name"),
    ([{**UNDER, "name": "Lower  hall"}], "not saved: region 'Lower  hall': a name has no #, no line break or tab,"
                                          " no space at either end, and single spaces between words"),
])
def test_a_save_with_a_region_that_is_not_valid_is_refused_and_the_file_stays(web, routes, regions, error):
    (routes / "map_389.regions.txt").write_text(GOOD)

    response = save(web, *regions)

    assert (response.status_code, response.json()["detail"]) == (422, error)
    assert (routes / "map_389.regions.txt").read_text() == GOOD


@pytest.mark.parametrize("body", [
    '{"regions": [{"name": "Hall", "low": 0, "high": 5, "outline": [[0, 0], [NaN, 0], [10, 10]]}]}',
    '{"regions": [{"name": "Hall", "low": 0, "high": Infinity, "outline": [[0, 0], [10, 0], [10, 10]]}]}',
])
def test_a_save_with_a_value_that_is_not_a_number_is_refused_and_the_file_stays(web, routes, body):
    (routes / "map_389.regions.txt").write_text(GOOD)

    response = web.post("/api/maps/drawn_cave/regions", content=body, headers={"content-type": "application/json"})

    assert (response.status_code, response.json()["detail"]) == (
        422, "not saved: region 'Hall': a height or an outline point is not a number")
    assert (routes / "map_389.regions.txt").read_text() == GOOD


def test_a_save_over_a_file_that_does_not_read_is_refused_and_the_file_stays(web, routes):
    bad = GOOD + "region 1 2 name Broken\n"
    (routes / "map_389.regions.txt").write_text(bad)

    response = save(web, UNDER)

    assert response.status_code == 422
    assert response.json()["detail"].endswith(
        "map_389.regions.txt: line 2: region 'Broken': the outline has 0 points; it needs at least 3")
    assert (routes / "map_389.regions.txt").read_text() == bad


@pytest.mark.parametrize("text, error", [
    ("region 3 13 0 0 10 0 10 10\n", "line 1: no `name <name>` at the end"),
    ("region 3 13 0 0 10 x 10 10 name A\n", "line 1: cannot read 'region 3 13 0 0 10 x 10 10 name A'"),
    ("region 3 13 0 0 10 0 10 name A\n", "line 1: give the low and high heights, then x y pairs"),
    ("# ok\nfloor 3 13 name A\n", "line 2: use: region <low> <high> <x1> <y1> ... name <name>"),
    (GOOD + GOOD, "line 2: region 'Ledge': two regions have this name"),
])
def test_a_regions_file_that_does_not_read_is_a_clear_error(web, routes, text, error):
    (routes / "map_389.regions.txt").write_text(text)

    response = web.get("/api/maps/drawn_cave/regions")

    assert response.status_code == 422
    assert response.json()["detail"] == f"{routes / 'map_389.regions.txt'}: {error}"


def test_a_map_with_no_regions_file_loads_as_it_does_now(web, routes):
    assert load(web) == {"file": "map_389.regions.txt", "exists": False, "regions": []}
    response = web.get("/api/maps/drawn_cave")
    assert response.status_code == 200, response.text
    assert "regions" not in response.json()
    assert not (routes / "map_389.regions.txt").exists()


def test_a_save_writes_only_the_regions_file_of_the_open_routes_map(web, routes):
    (routes / "map_36.regions.txt").write_text(GOOD)
    write_route(routes, "other", "route other 36\npoint p1 5.00 5.00 0.00 2.00\n")
    before = {p.name: p.read_text() for p in routes.iterdir()}

    save(web, LEDGE)

    after = {p.name: p.read_text() for p in routes.iterdir()}
    assert {name for name in after if after[name] != before.get(name)} == {"map_389.regions.txt"}


def test_the_regions_load_when_any_route_of_the_map_opens(web, routes):
    write_route(routes, "second_cave", "route second_cave 389\npoint p1 25.00 25.00 0.00 2.00\n")
    save(web, LEDGE)

    response = web.get("/api/maps/second_cave/regions")

    assert response.json()["regions"] == [LEDGE]


def test_regions_can_touch_at_their_borders(web):
    beside = {"name": "Beside", "low": 3.0, "high": 13.0, "outline": [[10.0, -10.0], [20.0, -10.0], [20.0, 10.0], [10.0, 10.0]]}

    assert save(web, LEDGE, beside).status_code == 200
    assert load(web)["regions"] == [LEDGE, beside]


def test_the_page_loads_the_regions_part(web):
    assert '<script src="/map_regions.js">' in web.get("/").text
    assert 'api("regions")' in web.get("/map_regions.js").text


def test_a_position_is_in_a_region_inside_its_outline_and_height_range_ends_included():
    # The module's query that later tickets (#184, #185) use; no web call shows it yet.
    import regions
    ledge = regions.Region("Ledge", 3.0, 13.0, [(0.0, -10.0), (10.0, -10.0), (10.0, 10.0), (0.0, 10.0)])
    beside = regions.Region("Beside", 3.0, 13.0, [(10.0, -10.0), (20.0, -10.0), (20.0, 10.0), (10.0, 10.0)])

    assert regions.regions_at([ledge, beside], 5, 5, 8) == ["Ledge"]
    assert regions.regions_at([ledge, beside], 5, 5, 0) == []  # the ground under the ledge
    assert regions.regions_at([ledge, beside], 5, 5, 13) == ["Ledge"]  # the top end
    assert regions.regions_at([ledge, beside], 5, 5, 3) == ["Ledge"]  # the low end
    assert regions.regions_at([ledge, beside], 25, 5, 8) == []  # outside the outlines
    assert regions.regions_at([ledge, beside], 10, 5, 8) == ["Ledge", "Beside"]  # on the shared border
    assert regions.regions_at([ledge, beside], 0, -10, 8) == ["Ledge"]  # on a corner


# Edits (ticket jvbastel/WOWAI#183): the page sends the whole edited list through the same checked save.

def test_a_save_that_moves_a_point_changes_the_heights_renames_one_and_deletes_another_reloads_changed(web, routes):
    third = {"name": "Third", "low": -2.0, "high": 2.0, "outline": [[20.0, 0.0], [30.0, 0.0], [30.0, 10.0]]}
    save(web, LEDGE, UNDER, third)

    moved = {**LEDGE, "outline": [[0.0, -10.0], [12.5, -11.25], [10.0, 10.0], [0.0, 10.0]]}
    heights = {**UNDER, "low": -4.5, "high": 6.25}
    renamed = {**third, "name": "Lower hall"}
    edited = [moved, {**heights, "name": "Under the ledge"}]  # UNDER renamed too, Third deleted below
    response = save(web, *edited)

    assert response.status_code == 200, response.text
    assert response.json()["regions"] == edited
    assert load(web)["regions"] == edited
    lines = (routes / "map_389.regions.txt").read_text().splitlines(keepends=True)
    assert "".join(line for line in lines if line.startswith("region")) == (
        "region 3.00 13.00 0.00 -10.00 12.50 -11.25 10.00 10.00 0.00 10.00 name Ledge\n"
        "region -4.50 6.25 0.00 0.00 10.00 0.00 10.00 10.00 name Under the ledge\n")
    assert save(web, *edited, renamed).status_code == 200
    assert [r["name"] for r in load(web)["regions"]] == ["Ledge", "Under the ledge", "Lower hall"]


def test_an_edit_that_gives_two_regions_one_name_is_refused_and_the_file_stays(web, routes):
    save(web, LEDGE, UNDER)
    before = (routes / "map_389.regions.txt").read_text()

    response = save(web, LEDGE, {**UNDER, "name": "Ledge", "low": -3.0})

    assert (response.status_code, response.json()["detail"]) == (
        422, "not saved: region 'Ledge': two regions have this name")
    assert (routes / "map_389.regions.txt").read_text() == before
    assert load(web)["regions"] == [LEDGE, UNDER]
