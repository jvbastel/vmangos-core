"""The player picks a region on the dungeon map and the other regions fade
or hide (ticket jvbastel/WOWAI#184, spec #179). The rules live in the
server: the map data marks each position with the regions it is in, and the
floor comes as one ground image per region plus one for the rest. These
tests drive the load-map, draft and walked calls as the page does.

The fixture cave (fixtures.py): the lower floor at z -20 (x 100..110,
y -50..-40), a bridge over its middle at z -12 (x 103..107, y -47..-43).
"""

import struct
import base64

import pytest
from fastapi.testclient import TestClient

import map_web
from fixtures import write_route, write_tile

# West and East split the lower floor at x 106; Bridge holds the upper floor.
REGIONS = """\
region -25 -15 102 -50 106 -50 106 -40 102 -40 name West
region -25 -15 106 -50 110 -50 110 -40 106 -40 name East
region -15 -8 103 -47 107 -47 107 -43 103 -43 name Bridge
"""

TRACE = """\
trace fixture_cave 389
p 105.00 -45.00 -12.00
p 105.00 -45.00 -20.00
p 106.00 -45.00 -20.00
p 101.00 -49.00 -20.00
"""


@pytest.fixture
def routes(tmp_path):
    return tmp_path / "routes"


@pytest.fixture
def web(tmp_path, routes):
    write_tile(tmp_path / "mmaps", 389)
    write_route(routes)
    (routes / "fixture_cave.trace.txt").write_text(TRACE)
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=routes, exports=routes)
    return TestClient(map_web.create_app(settings))


def with_regions(routes):
    (routes / "map_389.regions.txt").write_text(REGIONS)


def get(web, path, **params):
    response = web.get(path, params=params)
    assert response.status_code == 200, response.text
    return response.json()


def test_a_pack_and_a_route_point_carry_the_regions_they_are_in(web, routes):
    with_regions(routes)
    loaded = get(web, "/api/maps/fixture_cave")

    packs = {p["label"]: p["regions"] for p in loaded["packs"]}
    line = {p["name"]: p["regions"] for p in loaded["line"]}

    assert packs == {"pack1": ["East"], "bazzalan": []}  # bazzalan (101, -42) is outside every outline
    assert line == {"p1": [], "pull_pack1": ["West"], "p2": ["Bridge"], "pull_bazzalan": ["West"]}


def test_a_trace_point_carries_its_region_and_the_other_layer_of_ground_does_not(web, routes):
    with_regions(routes)
    points = get(web, "/api/maps/fixture_cave/draft", source="trace")["lines"][0]

    assert [((p["x"], p["y"], p["z"]), p["regions"]) for p in points] == [
        ((105.0, -45.0, -12.0), ["Bridge"]),       # on the bridge
        ((105.0, -45.0, -20.0), ["West"]),         # the same x, y under it
        ((106.0, -45.0, -20.0), ["West", "East"]),  # on the border of two regions
        ((101.0, -49.0, -20.0), []),               # outside every outline
    ]


def test_mobs_patrol_points_and_quest_marks_carry_the_regions_they_are_in(web, routes):
    from export_fixtures import EXPORT, write_export
    write_export(routes, text=EXPORT + "quest 5722\nvisit 105.50 -45.00 -12.00 Maur Grimtotem\n")
    with_regions(routes)
    loaded = get(web, "/api/maps/fixture_cave")

    patrol = loaded["patrols"][0]["points"]
    badges = {b["pack"]: b["regions"] for b in loaded["quest_badges"]}
    visit = next(o for q in loaded["quests"] for o in q["objectives"] if o["kind"] == "visit")

    assert [p["regions"] for p in patrol] == [["West"], ["West", "East"], ["West", "East"]]
    assert {w["guid"]: w["regions"] for w in loaded["wanderers"]} == {502: ["East"], 700: ["West"]}
    assert {b["name"]: b["regions"] for b in loaded["bosses"]} == {"Bazzalan": [], "Jergosh the Invoker": ["East"]}
    assert badges == {501: ["East"], 700: ["West"]}  # a quest badge sits on its pack
    assert visit["regions"] == ["Bridge"]


def test_a_walked_path_carries_the_regions_of_its_positions(web, routes, tmp_path):
    with_regions(routes)
    trace = tmp_path / "run.trace.txt"
    trace.write_text("1.0 tank at 105.0 -45.0 -12.0\n2.0 tank at 105.0 -45.0 -20.0\n")

    members = get(web, "/api/maps/fixture_cave/walked", trace=str(trace))["members"]

    assert [p["regions"] for p in members[0]["points"]] == [["Bridge"], ["West"]]


def png_size(data_url):
    png = base64.b64decode(data_url.removeprefix("data:image/png;base64,"))
    return struct.unpack(">II", png[16:24])  # the IHDR width and height


def keys_anywhere(value):
    if isinstance(value, dict):
        return set(value) | set().union(*map(keys_anywhere, value.values()))
    if isinstance(value, list):
        return set().union(*map(keys_anywhere, value))
    return set()


WHOLE_FLOOR = {"x_min": 100.0, "x_max": 114.0, "y_min": -50.0, "y_max": -34.0, "z_min": -20.0, "z_max": -12.0}


def test_the_floor_comes_as_one_ground_image_per_region_and_one_for_the_rest_with_the_same_bounds(web, routes):
    with_regions(routes)
    floor = get(web, "/api/maps/fixture_cave")["floor"]

    images = floor["images"]

    assert sorted(i["region"] or "" for i in images) == ["", "Bridge", "East", "West"]  # "" is the rest
    assert floor["bounds"] == WHOLE_FLOOR
    assert len({png_size(i["image"]) for i in images}) == 1
    assert all(i["image"].startswith("data:image/png;base64,") for i in images)


def test_a_map_with_no_regions_file_gives_the_map_data_as_before(web):
    loaded = get(web, "/api/maps/fixture_cave")
    draft = get(web, "/api/maps/fixture_cave/draft", source="trace")

    assert set(loaded["floor"]) == {"image", "bounds"}  # one image of all the ground
    assert loaded["floor"]["bounds"] == WHOLE_FLOOR
    assert "regions" not in keys_anywhere(loaded) | keys_anywhere(draft)


def test_the_map_request_has_no_height_filter(web):
    whole = get(web, "/api/maps/fixture_cave")["floor"]["bounds"]

    assert get(web, "/api/maps/fixture_cave", z_min=-15, z_max=0)["floor"]["bounds"] == whole


def test_the_page_has_no_heights_fields_or_show_button_and_loads_the_pick_part(web):
    page = web.get("/").text

    assert "Heights" not in page and 'id="zmin"' not in page and ">Show<" not in page
    assert '<script src="/region_pick.js">' in page
    assert web.get("/region_pick.js").status_code == 200


def test_a_snapped_point_carries_the_regions_of_the_floor_it_lands_on(web, routes):
    """A point clicked, dragged or moved to another floor in Draw mode."""
    with_regions(routes)

    assert get(web, "/api/maps/fixture_cave/snap", x=105, y=-45, z=-12)["point"]["regions"] == ["Bridge"]
    assert get(web, "/api/maps/fixture_cave/snap", x=105, y=-45, z=-20)["point"]["regions"] == ["West"]
