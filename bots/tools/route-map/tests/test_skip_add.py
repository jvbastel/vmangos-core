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


def test_the_loaded_map_flags_a_route_that_comes_within_the_aggro_range_of_a_skipped_pack(tmp_path):
    # Jergosh (pack 800, 109 -41 -20) is 6.6 yards from the piece of line from
    # p2 (105 -45 -12) to pull_bazzalan (102 -41 -20), at about 102.7 -41.9 -18.1.
    # As in the route library, the piece is named by its first point.
    order = load_order(app(tmp_path, "skip 800\n"))

    assert [(n["key"], n["distance"], n["point"]) for n in order["skipped_near"]] == [(800, 6.6, "p2")]
    assert order["skipped_near"][0]["text"] == (
        "the route passes the skipped pack 800 (Jergosh the Invoker) at 7 yards, within its aggro range of 20, "
        "by the point p2: it can join a fight as an extra pack")


def test_a_skipped_pack_out_of_the_aggro_range_of_the_route_is_not_flagged(tmp_path):
    from export_fixtures import EXPORT
    web = app(tmp_path, "skip 800\n")
    # Jergosh stands 40 yards north of the route.
    write_export(tmp_path / "exports", text=EXPORT.replace("mob 800 11518 109.00 -41.00", "mob 800 11518 149.00 -41.00"))

    assert load_order(web)["skipped_near"] == []


def test_saving_skips_gives_back_the_skipped_packs_near_the_route(tmp_path):
    response = app(tmp_path).post("/api/maps/fixture_cave/order", json={"packs": [], "skips": [800]})

    assert [n["key"] for n in response.json()["skipped_near"]] == [800]


def test_saving_refuses_a_pack_that_is_both_in_the_order_and_skipped(tmp_path):
    web = app(tmp_path, "pack 600\n")

    response = web.post("/api/maps/fixture_cave/order", json={"packs": [600], "skips": [600]})

    assert (response.status_code, response.json()["detail"]) == (422, "a pack is both in the order and skipped")
    assert (tmp_path / "routes" / ORDER).read_text() == "pack 600\n"


def test_a_skipped_pack_of_the_route_leaves_the_generators_order(tmp_path):
    order = load_order(app(tmp_path, "skip 502\n"))  # 502: a mob of pack1 (key 501)

    assert (order["hand"], order["skipped"], order["generator"]) == ([], [501], [600])


def test_saving_keeps_the_hand_comment_above_a_pack_that_is_now_skipped(tmp_path):
    from library_parse import library_order
    web = app(tmp_path, "# My cave.\n\n# Bazzalan hits hard.\npack 600\npack 501\n")

    web.post("/api/maps/fixture_cave/order", json={"packs": [501], "skips": [600]})

    text = (tmp_path / "routes" / ORDER).read_text()
    assert "# Bazzalan hits hard.\nskip 600 #" in text
    assert library_order(text) == {"packs": [501], "skips": [600]}


def test_the_page_part_has_skip_mode_and_sends_the_skips(tmp_path):
    script = app(tmp_path).get("/pack_order.js").text

    assert 'id="po-skip"' in script and "skips: skipped" in script and "skipped_near" in script
