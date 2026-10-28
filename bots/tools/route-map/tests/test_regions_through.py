"""The player draws the route on the ground under a faded or hidden region
(ticket jvbastel/WOWAI#185, spec #179). The page sends the regions that
clicks go through with the snap and path requests; the server leaves their
ground out of a snap. These tests drive the snap and path calls as the page
does.

The draw cave (draw_fixtures.py): the lower floor at z 0, the upper walk at
z 8 from the top of the ramp (x 30..40) round to a ledge over L1: at (5, 5)
the floors stack, z 0 and z 8. The region Upper holds the upper walk (and
the top of the ramp), Lower the lower floor.
"""

import pytest
from fastapi.testclient import TestClient

import map_web
from draw_fixtures import BORDER_X, CAVE, ISLAND
from fixtures import write_route, write_tile

ROUTE = "route drawn_cave 389\npoint p1 5.00 5.00 0.00 2.00\n"
REGIONS = """\
region 6 10 -5 -15 55 -15 55 12 -5 12 name Upper
region -2 2 -5 -15 55 -15 55 32 -5 32 name Lower
"""


@pytest.fixture
def routes(tmp_path):
    return tmp_path / "routes"


@pytest.fixture
def web(tmp_path, routes):
    write_tile(tmp_path / "mmaps", 389, polys=CAVE, next_tile=ISLAND, border_x=BORDER_X)
    write_route(routes, "drawn_cave", ROUTE)
    (routes / "map_389.regions.txt").write_text(REGIONS)
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=routes, exports=routes)
    return TestClient(map_web.create_app(settings))


def snap(web, **click):
    response = web.get("/api/maps/drawn_cave/snap", params=click)
    assert response.status_code == 200, response.text
    return response.json()


def test_a_click_through_the_upper_region_lands_on_the_lower_ground_and_the_heights_leave_the_upper_out(web):
    found = snap(web, x=5, y=5, through=["Upper"])
    near_top = snap(web, x=5, y=5, z=8, through=["Upper"])  # the last point was on the upper walk

    assert found["point"] == {"x": 5.0, "y": 5.0, "z": 0.0, "regions": ["Lower"]}
    assert found["floors"] == [0.0]
    assert near_top["point"]["z"] == 0.0


def test_a_click_with_no_regions_passed_snaps_as_before(web):
    assert snap(web, x=5, y=5) == {"point": {"x": 5.0, "y": 5.0, "z": 8.0, "regions": ["Upper"]}, "floors": [0.0, 8.0]}
    assert snap(web, x=5, y=5, z=1)["point"]["z"] == 0.0
    assert snap(web, x=8, y=13, z=0)["point"] == {"x": 8.0, "y": 10.0, "z": 0.0, "regions": ["Lower"]}  # in a wall


def test_a_click_where_all_the_ground_is_passed_through_still_lands_on_it(web):
    """No other ground there: the click lands on the ground of the region."""
    assert snap(web, x=45, y=5, through=["Upper"])["point"]["z"] == 8.0


def path(web, start, end, through):
    response = web.post("/api/maps/drawn_cave/path", json={"start": dict(zip("xyz", start)),
                                                           "end": dict(zip("xyz", end)), "through": through})
    assert response.status_code == 200, response.text
    return response.json()


def test_a_path_may_run_through_a_region_that_is_passed_through(web):
    """From the lower floor up the ramp onto the upper walk, with Upper faded."""
    piece = path(web, (25, 5, 0), (45, 5, 8), through=["Upper"])

    points = piece["points"]
    assert points[-1] == {"x": 45.0, "y": 5.0, "z": 8.0}
    assert any(p["z"] >= 6 for p in points[:-1])  # on the upper walk before the end
    assert piece["warnings"] == []


def test_a_path_refuses_a_through_that_is_not_a_list_of_names(web):
    response = web.post("/api/maps/drawn_cave/path", json={"start": {"x": 25, "y": 5, "z": 0},
                                                           "end": {"x": 45, "y": 5, "z": 8}, "through": "Upper"})
    assert response.status_code == 422
