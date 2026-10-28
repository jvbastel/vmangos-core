"""The player sets the pack order of a route on the dungeon map and saves it
to the hand pack order file of #147 (ticket jvbastel/WOWAI#155), through the
load-map call and the save-order call."""

from fastapi.testclient import TestClient

import map_web
from export_fixtures import write_export
from fixtures import write_route, write_tile


def app(tmp_path, order=None):
    write_tile(tmp_path / "mmaps", 389)
    write_route(tmp_path / "routes")
    write_export(tmp_path / "exports")
    if order is not None:
        (tmp_path / "routes" / "fixture_cave.order.txt").write_text(order)
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "exports")
    return TestClient(map_web.create_app(settings))


def load_order(tmp_path, order=None):
    response = app(tmp_path, order).get("/api/maps/fixture_cave")
    assert response.status_code == 200, response.text
    return response.json()["pack_order"]


def test_without_an_order_file_every_route_pack_has_the_generators_order(tmp_path):
    order = load_order(tmp_path)

    assert (order["hand"], order["generator"], order["exists"]) == ([], [501, 600], False)
    assert order["file"] == "fixture_cave.order.txt"


def test_an_existing_order_file_gives_the_hand_part_and_the_rest_keeps_the_generators_order(tmp_path):
    order = load_order(tmp_path, "# hand order\npack 600 # bazzalan\n\n")

    assert (order["hand"], order["generator"], order["exists"]) == ([600], [501], True)


def test_an_order_file_guid_names_the_pack_that_holds_that_mob(tmp_path):
    # 502 is pack1's second mob, 701 the second mob of pack 700 (off the route).
    order = load_order(tmp_path, "pack 502\npack 701\n")

    assert (order["hand"], order["generator"]) == ([501, 700], [600])
    assert order["warnings"] == []


def test_a_guid_of_no_pack_of_the_map_is_left_out_with_a_warning(tmp_path):
    order = load_order(tmp_path, "pack 999\npack 600\n")

    assert order["hand"] == [600]
    assert order["warnings"] == ["fixture_cave.order.txt: line 1: the guid 999 is not a mob of a pack of the map; left out"]


def test_two_guids_of_one_pack_keep_the_first_place_with_a_warning(tmp_path):
    order = load_order(tmp_path, "pack 502\npack 600\npack 501\n")

    assert order["hand"] == [501, 600]
    assert order["warnings"] == ["fixture_cave.order.txt: line 3: the guid 501 is in pack 501, already at place 1; left out"]


def test_an_order_file_that_does_not_read_is_a_clear_error(tmp_path):
    response = app(tmp_path, "pack 600\npack six\n").get("/api/maps/fixture_cave")

    assert response.status_code == 422
    assert "fixture_cave.order.txt: line 2: use: pack <spawn guid>" in response.json()["detail"]


def test_two_route_packs_of_one_export_pack_are_one_pack_of_the_map_with_a_warning(tmp_path):
    from fixtures import ROUTE
    web = app(tmp_path)
    # A stale route: the export's pack 501 is two packs of the route.
    write_route(tmp_path / "routes", text=ROUTE.replace(
        "mob 502 11319 # Ragefire Shaman\n",
        "pack pack2 109.00 -46.00 -20.00\nmob 502 11319 # Ragefire Shaman\n"))
    loaded = web.get("/api/maps/fixture_cave").json()

    assert [(p["key"], p["label"], [m["guid"] for m in p["mobs"]]) for p in loaded["packs"]] == [
        (501, "pack1", [501, 502]), (600, "bazzalan", [600]), (700, None, [700, 701]), (800, None, [800])]
    assert loaded["pack_order"]["generator"] == [501, 600]
    assert loaded["pack_order"]["warnings"] == [
        "the route packs pack1 and pack2 are one pack of the export (key 501): shown as pack1"]
