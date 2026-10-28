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
    assert "fixture_cave.order.txt: line 2: use: pack <spawn guid>, or skip <spawn guid>" in response.json()["detail"]


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


def test_saving_the_order_writes_pack_lines_that_the_route_library_parses(tmp_path):
    from library_parse import library_pack_order
    web = app(tmp_path)

    response = web.post("/api/maps/fixture_cave/order", json={"packs": [600, 700, 501]})

    assert response.status_code == 200, response.text
    text = (tmp_path / "routes" / "fixture_cave.order.txt").read_text()
    assert library_pack_order(text) == [600, 700, 501]
    assert "pack 600 # bazzalan, Bazzalan\n" in text


def test_saving_writes_only_the_order_file_of_the_open_route(tmp_path):
    web = app(tmp_path)
    routes = tmp_path / "routes"
    (routes / "other_cave.order.txt").write_text("pack 1\n")
    before = {p.name: p.read_text() for p in routes.iterdir()}

    web.post("/api/maps/fixture_cave/order", json={"packs": [600]})

    after = {p.name: p.read_text() for p in routes.iterdir()}
    assert {name for name in after if after[name] != before.get(name)} == {"fixture_cave.order.txt"}


def test_the_saved_order_reloads_unchanged(tmp_path):
    web = app(tmp_path, "pack 502\n")

    web.post("/api/maps/fixture_cave/order", json={"packs": [600, 501]})
    order = web.get("/api/maps/fixture_cave").json()["pack_order"]

    assert (order["hand"], order["generator"], order["warnings"]) == ([600, 501], [], [])


def test_saving_refuses_a_pack_that_is_not_on_the_map_or_comes_twice(tmp_path):
    web = app(tmp_path, "pack 600\n")

    unknown = web.post("/api/maps/fixture_cave/order", json={"packs": [600, 999]})
    twice = web.post("/api/maps/fixture_cave/order", json={"packs": [600, 600]})

    assert (unknown.status_code, unknown.json()["detail"]) == (422, "999 is not the key of a pack of the map")
    assert (twice.status_code, twice.json()["detail"]) == (422, "a pack comes twice in the order")
    assert (tmp_path / "routes" / "fixture_cave.order.txt").read_text() == "pack 600\n"


def test_an_empty_hand_order_gives_every_pack_back_to_the_generator(tmp_path):
    from library_parse import library_pack_order
    web = app(tmp_path, "pack 600\n")

    web.post("/api/maps/fixture_cave/order", json={"packs": []})

    assert library_pack_order((tmp_path / "routes" / "fixture_cave.order.txt").read_text()) == []
    assert web.get("/api/maps/fixture_cave").json()["pack_order"]["generator"] == [501, 600]


def test_the_page_loads_the_pack_order_part(tmp_path):
    web = app(tmp_path)

    assert '<script src="/pack_order.js">' in web.get("/").text
    assert "/order" in web.get("/pack_order.js").text


HAND_NOTES = """\
# My header for the cave.
# It says how I chose the order.

# The shaman room.
pack 502 # pack1, Ragefire Trogg, Ragefire Shaman

# Bazzalan last of the hand part.
# He hits hard.
pack 600 # bazzalan, Bazzalan
pack 700
"""


def save(tmp_path, order, packs):
    web = app(tmp_path, order)
    response = web.post("/api/maps/fixture_cave/order", json={"packs": packs})
    assert response.status_code == 200, response.text
    return web, (tmp_path / "routes" / "fixture_cave.order.txt").read_text()


def test_saving_keeps_the_header_comments_of_the_order_file_at_the_top(tmp_path):
    _, text = save(tmp_path, HAND_NOTES, [700, 600, 501])

    assert text.startswith("# My header for the cave.\n# It says how I chose the order.\n\n")
    assert "WOWAI#155" not in text


def test_saving_moves_the_comment_block_above_a_pack_with_that_pack(tmp_path):
    from library_parse import library_pack_order
    web, text = save(tmp_path, HAND_NOTES, [600, 501, 700])

    assert "# Bazzalan last of the hand part.\n# He hits hard.\npack 600 #" in text
    assert "# The shaman room.\npack 501 #" in text
    assert text.index("pack 600") < text.index("# The shaman room.") < text.index("pack 501") < text.index("pack 700")
    assert library_pack_order(text) == [600, 501, 700]
    assert web.get("/api/maps/fixture_cave").json()["pack_order"]["hand"] == [600, 501, 700]


def test_saving_keeps_the_comments_of_a_pack_given_back_to_the_generator(tmp_path):
    _, text = save(tmp_path, HAND_NOTES, [700])

    assert "# The shaman room.\n" in text
    assert "# Bazzalan last of the hand part.\n# He hits hard.\n" in text
    assert text.index("pack 700") < text.index("# The shaman room.")
    assert "pack 501" not in text and "pack 600" not in text


def test_saving_twice_keeps_the_comments_unchanged(tmp_path):
    web, first = save(tmp_path, HAND_NOTES, [600, 501, 700])

    web.post("/api/maps/fixture_cave/order", json={"packs": [600, 501, 700]})

    assert (tmp_path / "routes" / "fixture_cave.order.txt").read_text() == first


def test_saving_the_ragefire_order_in_a_new_order_keeps_every_hand_comment():
    # The committed order file is only read: the save goes to a string.
    import pack_order
    from pathlib import Path
    real = Path(__file__).resolve().parents[3] / "data" / "routes" / "ragefire_chasm.order.txt"
    text = real.read_text()
    guids = [guid for _, _, guid in pack_order.parse(text)]
    packs = [{"key": g, "label": f"p{g}", "mobs": [{"guid": g, "name": "Mob", "entry": 1}]} for g in guids]

    saved = pack_order.write("ragefire_chasm", list(reversed(guids)), packs, text)

    comments = [line for line in text.splitlines() if line.startswith("#")]
    assert [line for line in saved.splitlines() if line.startswith("#")] != comments  # the blocks moved
    assert sorted(line for line in saved.splitlines() if line.startswith("#")) == sorted(comments)
    assert saved.startswith(text.split("\n\n", 1)[0] + "\n\n")
    assert saved.index("# The first trogg, on the ramp.\npack 48648 #") > saved.index("pack 43582")
    assert [guid for _, _, guid in pack_order.parse(saved)] == list(reversed(guids))


def test_the_loaded_map_has_both_the_pack_order_and_the_quest_badges(tmp_path):
    # #154 and #155 both add parts to the load-map call.
    loaded = app(tmp_path, "pack 600\n").get("/api/maps/fixture_cave").json()

    assert (loaded["pack_order"]["hand"], loaded["pack_order"]["generator"]) == ([600], [501])
    assert [p["key"] for p in loaded["packs"]] == [501, 600, 700, 800]
    assert [(b["pack"], b["quest"], b["count"]) for b in loaded["quest_badges"]] == [(501, 5723, 1), (700, 5723, 1)]
    assert [q["quest"] for q in loaded["quests"]] == [5723]


def test_saving_over_an_order_file_that_does_not_read_is_a_clear_error_and_keeps_it(tmp_path):
    web = app(tmp_path, "pack 600\npack six\n")

    response = web.post("/api/maps/fixture_cave/order", json={"packs": [600]})

    assert response.status_code == 422
    assert "fixture_cave.order.txt: line 2: use: pack <spawn guid>, or skip <spawn guid>" in response.json()["detail"]
    assert (tmp_path / "routes" / "fixture_cave.order.txt").read_text() == "pack 600\npack six\n"


def test_saving_keeps_the_hand_comment_at_the_end_of_an_old_pack_line(tmp_path):
    _, text = save(tmp_path, "pack 600 # the boss, pull him to the door\npack 502\n", [501, 600])

    assert "pack 600 # the boss, pull him to the door\n" in text
    assert "pack 501 # pack1, Ragefire Trogg, Ragefire Shaman\n" in text
