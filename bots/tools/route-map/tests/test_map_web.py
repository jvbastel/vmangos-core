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


def test_a_point_of_the_fixture_tile_comes_back_at_its_wow_position_on_each_stacked_floor(tmp_path):
    web = client(tmp_path)

    stacked = web.get("/api/maps/fixture_cave/floors", params={"x": 105, "y": -45}).json()["floors"]
    lava = web.get("/api/maps/fixture_cave/floors", params={"x": 112, "y": -45}).json()["floors"]
    outside = web.get("/api/maps/fixture_cave/floors", params={"x": 105, "y": -55}).json()["floors"]

    assert stacked == [{"z": -20.0, "lava": False}, {"z": -12.0, "lava": False}]
    assert lava == [{"z": -20.0, "lava": True}]
    assert outside == []


def test_a_navmesh_of_another_version_is_a_clear_error(tmp_path):
    for case, (version, words) in enumerate([({"mmap_version": 5}, "mmap version 5"), ({"dt_version": 6}, "Detour version 6"),
                           ({"link_size": 12}, "64-bit poly refs")]):
        response = client(tmp_path / str(case), **version).get("/api/maps/fixture_cave")

        assert response.status_code == 422
        assert words in response.json()["detail"]


def test_a_map_with_no_navmesh_tiles_is_a_clear_error(tmp_path):
    write_route(tmp_path / "routes", text="route fixture_cave 33\n")
    response = TestClient(map_web.create_app(settings(tmp_path))).get("/api/maps/fixture_cave")

    assert response.status_code == 422
    assert "no navmesh tiles of map 33" in response.json()["detail"]


def test_the_map_has_the_floor_image_with_the_wow_bounds_of_the_floor(tmp_path):
    floor = client(tmp_path).get("/api/maps/fixture_cave").json()["floor"]

    assert floor["image"].startswith("data:image/png;base64,")
    assert floor["bounds"] == {"x_min": 100.0, "x_max": 114.0, "y_min": -50.0, "y_max": -34.0,
                               "z_min": -20.0, "z_max": -12.0}


def test_the_floor_is_only_the_floor_joined_to_the_route(tmp_path):
    bounds = client(tmp_path).get("/api/maps/fixture_cave").json()["floor"]["bounds"]

    assert bounds["x_max"] == 114.0  # the lava pool over the tile border, not the plane at x 300 to 330
    assert bounds["y_max"] == -34.0  # the lava pool behind the steep bank
    assert bounds["z_max"] == -12.0  # the upper floor holds the route point p2


def test_a_height_range_draws_only_the_floors_in_it(tmp_path):
    web = client(tmp_path)

    upper = web.get("/api/maps/fixture_cave", params={"z_min": -15}).json()["floor"]["bounds"]
    lower = web.get("/api/maps/fixture_cave", params={"z_max": -15}).json()["floor"]["bounds"]
    empty = web.get("/api/maps/fixture_cave", params={"z_min": 0})

    assert upper == {"x_min": 103.0, "x_max": 107.0, "y_min": -47.0, "y_max": -43.0, "z_min": -12.0, "z_max": -12.0}
    assert lower == {"x_min": 100.0, "x_max": 114.0, "y_min": -50.0, "y_max": -34.0, "z_min": -20.0, "z_max": -19.0}
    assert empty.status_code == 422
    assert "no floor between" in empty.json()["detail"]


def test_the_page_is_served(tmp_path):
    response = client(tmp_path).get("/")

    assert response.status_code == 200
    assert "Dungeon map" in response.text


def write_settings(tmp_path):
    path = tmp_path / "settings.toml"
    path.write_text('[paths]\nnavmesh = "/data/mmaps"\nroutes = "routes"\nexports = "exports"\n')
    return path


def test_the_command_takes_the_navmesh_folder_from_the_settings_file(tmp_path):
    command = map_web.read_command(["ragefire_chasm", "--settings", str(write_settings(tmp_path))])

    assert command.route == "ragefire_chasm"
    assert command.settings.navmesh == map_web.Path("/data/mmaps")
    assert command.settings.routes == tmp_path / "routes"  # relative to the settings file
    assert command.settings.exports == tmp_path / "exports"


def test_the_navmesh_option_overrides_the_settings_file(tmp_path):
    command = map_web.read_command(["ragefire_chasm", "--settings", str(write_settings(tmp_path)),
                                    "--navmesh", str(tmp_path / "other")])

    assert command.settings.navmesh == tmp_path / "other"
