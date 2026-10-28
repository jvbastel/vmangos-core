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
