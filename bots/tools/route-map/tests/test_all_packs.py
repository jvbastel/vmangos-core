"""The dungeon map shows every pack of the export file, its patrols, its
wanderers and its bosses (ticket jvbastel/WOWAI#153), through the load-map
call."""

from fastapi.testclient import TestClient

import map_web
from export_fixtures import write_export
from fixtures import write_route, write_tile


def load(tmp_path, export=True):
    write_tile(tmp_path / "mmaps", 389)
    write_route(tmp_path / "routes")
    if export:
        write_export(tmp_path / "exports")
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "exports")
    return TestClient(map_web.create_app(settings)).get("/api/maps/fixture_cave").json()


def test_the_map_holds_every_pack_of_the_export_with_its_on_route_flag(tmp_path):
    packs = load(tmp_path)["packs"]

    assert [(p["key"], p["on_route"], p["label"], p["order"]) for p in packs] == [
        (501, True, "pack1", 1), (600, True, "bazzalan", 2), (700, False, None, None), (800, False, None, None)]


def test_a_pack_off_the_route_has_its_place_mobs_and_aggro_radius(tmp_path):
    off = load(tmp_path)["packs"][2]

    assert (off["x"], off["y"], off["z"]) == (102.0, -48.0, -20.0)  # its first mob
    assert off["mobs"] == [{"guid": 700, "entry": 11318, "name": "Ragefire Trogg"},
                           {"guid": 701, "entry": 11319, "name": "Ragefire Shaman"}]
    assert off["aggro_radius"] == 20.0  # the generator's aggro range
    assert off["pull_point"] is None
    assert off["bosses"] == []


def test_a_pack_on_the_route_keeps_its_pull_point(tmp_path):
    pack1 = load(tmp_path)["packs"][0]

    assert pack1["pull_point"] == {"name": "pull_pack1", "x": 104.0, "y": -48.0, "z": -20.0}
    assert pack1["aggro_radius"] == 20.0


def test_boss_packs_carry_the_boss_names_and_the_bosses_are_listed_in_run_order(tmp_path):
    loaded = load(tmp_path)

    assert [p["bosses"] for p in loaded["packs"]] == [[], ["Bazzalan"], [], ["Jergosh the Invoker"]]
    assert loaded["bosses"] == [
        {"entry": 11519, "name": "Bazzalan", "pack": 600, "x": 101.0, "y": -42.0, "z": -20.0},
        {"entry": 11518, "name": "Jergosh the Invoker", "pack": 800, "x": 109.0, "y": -41.0, "z": -20.0}]


def test_the_map_holds_the_patrols_with_their_waypoints_in_walk_order(tmp_path):
    assert load(tmp_path)["patrols"] == [
        {"guid": 701, "entry": 11319, "name": "Ragefire Shaman", "pack": 700,
         "points": [{"x": 103.0, "y": -48.5, "z": -20.0}, {"x": 106.0, "y": -48.0, "z": -20.0},
                    {"x": 106.0, "y": -44.0, "z": -20.0}]}]


def test_the_map_holds_the_wanderers_with_their_wander_circles(tmp_path):
    assert load(tmp_path)["wanderers"] == [
        {"guid": 502, "entry": 11319, "name": "Ragefire Shaman", "pack": 501, "x": 109.0, "y": -46.0, "z": -20.0, "radius": 2.0},
        {"guid": 700, "entry": 11318, "name": "Ragefire Trogg", "pack": 700, "x": 102.0, "y": -48.0, "z": -20.0, "radius": 5.0}]


def test_without_an_export_file_the_map_shows_the_route_packs_only(tmp_path):
    loaded = load(tmp_path, export=False)

    assert [(p["key"], p["on_route"]) for p in loaded["packs"]] == [(501, True), (600, True)]
    assert (loaded["patrols"], loaded["wanderers"], loaded["bosses"]) == ([], [], [])


def test_an_export_file_that_does_not_read_is_a_clear_error(tmp_path):
    write_tile(tmp_path / "mmaps", 389)
    write_route(tmp_path / "routes")
    write_export(tmp_path / "routes", text="export 389\nmob 1 2 3 4 5 6\n")
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "routes")
    response = TestClient(map_web.create_app(settings)).get("/api/maps/fixture_cave")

    assert response.status_code == 422
    assert "map_389.export.txt: line 2: a mob line before any pack line" in response.json()["detail"]


def test_the_page_loads_the_pack_drawing(tmp_path):
    write_route(tmp_path / "routes")
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "routes")
    web = TestClient(map_web.create_app(settings))

    assert '<script src="/map_packs.js">' in web.get("/").text
    assert "mapdata" in web.get("/map_packs.js").text
