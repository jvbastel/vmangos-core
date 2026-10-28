"""The route drawing through the web API: snap a click to the floor, join
two clicks with a floor path, save the trace (ticket jvbastel/WOWAI#157)."""

import math

import pytest
from fastapi.testclient import TestClient

import map_web
import trace_file
from draw_fixtures import BORDER_X, CAVE, ISLAND
from fixtures import write_route, write_tile

ROUTE = "route drawn_cave 389\npoint p1 5.00 5.00 0.00 2.00\n"


@pytest.fixture
def web(tmp_path):
    write_tile(tmp_path / "mmaps", 389, polys=CAVE, next_tile=ISLAND, border_x=BORDER_X)
    write_route(tmp_path / "routes", "drawn_cave", ROUTE)
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "routes")
    return TestClient(map_web.create_app(settings))


def snap(web, **click):
    response = web.get("/api/maps/drawn_cave/snap", params=click)
    assert response.status_code == 200, response.text
    return response.json()


def test_a_click_where_floors_stack_lands_on_the_floor_nearest_the_height_asked(web):
    low = snap(web, x=5, y=5, z=1)
    high = snap(web, x=5, y=5, z=7)

    assert low["point"] == {"x": 5.0, "y": 5.0, "z": 0.0}
    assert high["point"] == {"x": 5.0, "y": 5.0, "z": 8.0}
    assert low["floors"] == [0.0, 8.0]  # the page lets the player choose one of them


def test_a_click_with_no_height_lands_on_the_top_floor(web):
    assert snap(web, x=5, y=5)["point"]["z"] == 8.0
    assert snap(web, x=25, y=25)["point"] == {"x": 25.0, "y": 25.0, "z": 0.0}


def test_a_click_on_the_ramp_lands_at_the_ramp_height(web):
    assert snap(web, x=35, y=5)["point"]["z"] == pytest.approx(4.0)


def test_a_click_in_a_wall_lands_on_the_nearest_floor(web):
    point = snap(web, x=8, y=13, z=0)["point"]  # in the wall block, 3 yards from L1

    assert point == {"x": 8.0, "y": 10.0, "z": 0.0}


def path(web, start, end):
    response = web.post("/api/maps/drawn_cave/path", json={"start": dict(zip("xyz", start)), "end": dict(zip("xyz", end))})
    assert response.status_code == 200, response.text
    return response.json()


def on_floor(web, point):
    return any(abs(z - point["z"]) < 0.05 for z in snap(web, x=point["x"], y=point["y"])["floors"])


def test_a_path_between_two_points_goes_round_the_wall(web):
    piece = path(web, (5, 5, 0), (5, 25, 0))  # the wall block stands between them

    points = piece["points"]
    assert (points[0], points[-1]) == ({"x": 5.0, "y": 5.0, "z": 0.0}, {"x": 5.0, "y": 25.0, "z": 0.0})
    assert max(p["x"] for p in points) >= 20  # round the east end of the wall, through L4
    assert all(not (p["x"] < 19.9 and 10.1 < p["y"] < 19.9) for p in points)  # never in the wall
    assert all(on_floor(web, p) and p["z"] == 0.0 for p in points)
    assert piece["straight"] == 20.0
    assert 40 <= piece["length"] <= 45  # 5,5 -> 20,10 -> 20,20 -> 5,25: 15.8 + 10 + 15.8
    assert piece["warnings"] == []


def test_a_path_up_the_ramp_follows_the_ramp_height(web):
    piece = path(web, (25, 5, 0), (45, 5, 8))

    on_ramp = [p for p in piece["points"] if 30 <= p["x"] <= 40]
    assert len(on_ramp) >= 3
    assert all(abs(p["z"] - 8 * (p["x"] - 30) / 10) < 0.05 for p in on_ramp)
    assert piece["warnings"] == []


def test_a_path_over_lava_gets_a_warning(web):
    piece = path(web, (5, 5, 0), (-15, 5, 0))  # the island J: the only way is over the lava cell I

    assert [w["kind"] for w in piece["warnings"]] == ["lava"]
    assert piece["points"][-1] == {"x": -15.0, "y": 5.0, "z": 0.0}


def test_a_path_to_another_level_by_a_long_way_round_gets_a_warning(web):
    piece = path(web, (5, 5, 0), (5, 5, 8))  # from L1 to the ledge over it: round by the ramp

    assert [w["kind"] for w in piece["warnings"]] == ["level"]
    assert piece["length"] > 60  # 5,5 -> 40,0 up the ramp -> 40,0 -> 5,5 on the upper walk
    assert max(p["z"] for p in piece["points"]) == 8.0


def test_a_path_that_keeps_off_lava_when_it_can_has_no_warning(web):
    assert path(web, (5, 5, 0), (25, 25, 0))["warnings"] == []
    assert path(web, (25, 5, 0), (45, 5, 8))["warnings"] == []  # up the ramp to another level: the short way


def test_two_points_with_no_floor_path_get_a_straight_line_and_a_warning(tmp_path):
    from draw_fixtures import cell
    from fixtures import floor
    write_tile(tmp_path / "mmaps", 389, polys=[floor(cell(0, 0)), floor(cell(30, 0))], next_tile=None)
    write_route(tmp_path / "routes", "drawn_cave", ROUTE)
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "routes")

    piece = path(TestClient(map_web.create_app(settings)), (5, 5, 0), (35, 5, 0))

    assert [w["kind"] for w in piece["warnings"]] == ["no_path"]
    assert len(piece["points"]) == 2


def test_saving_the_trace_writes_dense_points_at_floor_height_that_the_route_library_parses(web, tmp_path):
    routes = tmp_path / "routes"
    (routes / "other_cave.trace.txt").write_text("trace other_cave 389\np 1.00 2.00 3.00\n")
    before = {p.name: p.read_text() for p in routes.iterdir()}
    clicks = [(5, 5, 0), (5, 25, 0), (45, 5, 8)]

    response = web.post("/api/maps/drawn_cave/trace", json={"points": [dict(zip("xyz", c)) for c in clicks]})

    assert response.status_code == 200, response.text
    after = {p.name: p.read_text() for p in routes.iterdir()}
    assert set(after) - set(before) == {"drawn_cave.trace.txt"}
    assert {k: after[k] for k in before} == before  # only the open route's trace file is written
    trace = trace_file.parse(after["drawn_cave.trace.txt"])
    assert (trace.name, trace.map_id) == ("drawn_cave", 389)
    points = trace.points
    assert [(p.x, p.y, p.z) for p in (points[0], points[-1])] == [(5.0, 5.0, 0.0), (45.0, 5.0, 8.0)]
    assert (5.0, 25.0, 0.0) in [(p.x, p.y, p.z) for p in points]  # each click is a point of the trace
    assert all(p.joined and not p.mark for p in points[1:])
    assert len(points) > 30
    assert all(math.dist((a.x, a.y, a.z), (b.x, b.y, b.z)) <= 3.5 for a, b in zip(points, points[1:]))
    assert all(on_floor(web, {"x": p.x, "y": p.y, "z": p.z}) for p in points)
    assert response.json()["points"] == len(points)


def test_saving_a_trace_returns_the_warnings_of_its_pieces(web):
    clicks = [(5, 5, 0), (5, 25, 0), (25, 25, 0), (5, 5, 0), (-15, 5, 0)]

    saved = web.post("/api/maps/drawn_cave/trace", json={"points": [dict(zip("xyz", c)) for c in clicks]}).json()

    assert [(w["piece"], w["kind"]) for w in saved["warnings"]] == [(4, "lava")]
