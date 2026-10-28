"""The player marks skipped packs and adds packs that the route does not
pull yet, on the dungeon map (ticket jvbastel/WOWAI#156), through the
load-map call and the save-order call. The order file's `skip` lines and
the `pack` lines of added packs are those of the route library (#151)."""

from fastapi.testclient import TestClient

import map_web
from export_fixtures import write_export
from fixtures import write_route, write_tile

ORDER = "fixture_cave.order.txt"


def app(tmp_path, order=None):
    write_tile(tmp_path / "mmaps", 389)
    write_route(tmp_path / "routes")
    write_export(tmp_path / "exports")
    if order is not None:
        (tmp_path / "routes" / ORDER).write_text(order)
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "exports")
    return TestClient(map_web.create_app(settings))


def load_order(web):
    response = web.get("/api/maps/fixture_cave")
    assert response.status_code == 200, response.text
    return response.json()["pack_order"]


def test_saving_skips_and_an_added_pack_writes_lines_that_the_route_library_parses_and_they_load_back(tmp_path):
    from library_parse import library_order
    web = app(tmp_path)

    # 600 (Bazzalan) is on the route, 700 is not: it is added. 501 and 800 are skipped.
    response = web.post("/api/maps/fixture_cave/order", json={"packs": [600, 700], "skips": [800, 501]})

    assert response.status_code == 200, response.text
    text = (tmp_path / "routes" / ORDER).read_text()
    assert library_order(text) == {"packs": [600, 700], "skips": [800, 501]}
    assert "pack 700 # off the route, Ragefire Trogg, Ragefire Shaman\n" in text
    assert "skip 800 # off the route, Jergosh the Invoker\n" in text
    order = load_order(web)
    assert (order["hand"], order["skipped"], order["generator"], order["warnings"]) == ([600, 700], [800, 501], [], [])
