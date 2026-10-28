"""A route on a continent map (0 Eastern Kingdoms, 1 Kalimdor) is refused
before any navmesh tile is read: a continent has thousands of tiles and its
load filled a 24 GB VM. These tests use a tiny fixture route on map 1 and a
navmesh folder with no real tiles; a tile read fails the test."""

import pytest
from fastapi.testclient import TestClient

import map_web
import navmesh
from fixtures import write_route

KALIMDOR_ROUTE = """\
route fixture_kalimdor 1
point p1 101.00 -49.00 -20.00 2.00
"""


@pytest.fixture
def no_tile_reads(monkeypatch):
    def read_tile(path):
        raise AssertionError(f"read a navmesh tile: {path}")
    monkeypatch.setattr(navmesh, "read_tile", read_tile)


def settings(tmp_path, map_id):
    write_route(tmp_path / "routes", name="fixture_kalimdor", text=KALIMDOR_ROUTE.replace("kalimdor 1", f"kalimdor {map_id}"))
    (tmp_path / "mmaps").mkdir()
    (tmp_path / "mmaps" / f"{map_id:03d}3131.mmtile").write_bytes(b"not a tile")
    return map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "routes")


@pytest.mark.parametrize("map_id, name", [(0, "Eastern Kingdoms"), (1, "Kalimdor")])
def test_the_api_refuses_a_continent_route_with_a_4xx_and_the_reason(tmp_path, no_tile_reads, map_id, name):
    web = TestClient(map_web.create_app(settings(tmp_path, map_id)))

    for response in (web.get("/api/maps/fixture_kalimdor"),
                     web.get("/api/maps/fixture_kalimdor/floors", params={"x": 101, "y": -49})):
        assert response.status_code == 422
        assert f"map {map_id} ({name}) is a continent" in response.json()["detail"]


def test_the_command_exits_with_a_message_on_a_continent_route(tmp_path, no_tile_reads, monkeypatch):
    toml = tmp_path / "settings.toml"
    settings(tmp_path, 1)
    toml.write_text('[paths]\nnavmesh = "mmaps"\nroutes = "routes"\nexports = "routes"\n')
    monkeypatch.setattr(map_web, "create_app", lambda s: pytest.fail("the command started the map server"))

    with pytest.raises(SystemExit) as stop:
        map_web.main(["fixture_kalimdor", "--settings", str(toml), "--no-browser"])

    assert "map 1 (Kalimdor) is a continent" in str(stop.value.code)
