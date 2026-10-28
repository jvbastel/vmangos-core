"""The route drawing through the web API: snap a click to the floor, join
two clicks with a floor path, save the trace (ticket jvbastel/WOWAI#157)."""

import pytest
from fastapi.testclient import TestClient

import map_web
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
