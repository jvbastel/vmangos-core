from fastapi.testclient import TestClient

import map_web
from fixtures import write_route, write_tile


def settings(tmp_path):
    return map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "routes")


def client(tmp_path, **tile):
    write_tile(tmp_path / "mmaps", 389, **tile)
    write_route(tmp_path / "routes")
    return TestClient(map_web.create_app(settings(tmp_path)))


def test_the_map_has_the_route_line_in_route_order(tmp_path):
    loaded = client(tmp_path).get("/api/maps/fixture_cave").json()

    assert loaded["route"] == "fixture_cave"
    assert loaded["map_id"] == 389
    assert [(p["name"], p["x"], p["y"], p["z"]) for p in loaded["line"]] == [
        ("p1", 101.0, -49.0, -20.0), ("pull_pack1", 104.0, -48.0, -20.0),
        ("p2", 105.0, -45.0, -12.0), ("pull_bazzalan", 102.0, -41.0, -20.0)]


def test_the_map_has_the_route_packs_with_label_order_place_mobs_and_pull_point(tmp_path):
    packs = client(tmp_path).get("/api/maps/fixture_cave").json()["packs"]

    assert [(p["label"], p["order"], p["key"]) for p in packs] == [("pack1", 1, 501), ("bazzalan", 2, 600)]
    assert (packs[0]["x"], packs[0]["y"], packs[0]["z"]) == (108.0, -45.0, -20.0)
    assert packs[0]["mobs"] == [{"guid": 501, "entry": 11318, "name": "Ragefire Trogg"},
                                {"guid": 502, "entry": 11319, "name": "Ragefire Shaman"}]
    assert packs[0]["pull_point"] == {"name": "pull_pack1", "x": 104.0, "y": -48.0, "z": -20.0}
    assert packs[1]["pull_point"]["name"] == "pull_bazzalan"
