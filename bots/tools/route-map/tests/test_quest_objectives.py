"""The dungeon map shows the quest objectives of the dungeon, and what the
route meets and does not meet (ticket jvbastel/WOWAI#154), through the
load-map call.

The fixture route pulls pack 501 (one Trogg 11318, one Shaman 11319) and
pack 600 (Bazzalan 11519). Off the route: pack 700 (one Trogg, one Shaman)
and pack 800 (Jergosh 11518). Its line passes p2 at (105, -45, -12).
"""

from fastapi.testclient import TestClient

import map_web
from export_fixtures import EXPORT, write_export
from fixtures import write_route, write_tile

QUESTS = """\
quest 5722
visit 105.50 -45.00 -12.00 Maur Grimtotem
quest 5724
visit 90.00 -60.00 -20.00 Lost Satchel
quest 5725
item 14395 1 11319 11322
quest 5728
kill 11519 1
kill 11518 1
questname 5722 Searching for the Lost Satchel
questname 5724 Returning the Lost Satchel
questname 5725 The Power to Destroy...
questname 5728 Hidden Enemies
itemname 14395 Spells of Shadow
"""


def load(tmp_path):
    write_tile(tmp_path / "mmaps", 389)
    write_route(tmp_path / "routes")
    write_export(tmp_path / "exports", text=EXPORT + QUESTS)
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "exports")
    response = TestClient(map_web.create_app(settings)).get("/api/maps/fixture_cave")
    assert response.status_code == 200, response.text
    return response.json()


def quest(loaded, number):
    return next(q for q in loaded["quests"] if q["quest"] == number)


def test_a_kill_objective_the_route_does_not_meet_names_the_packs_that_could_meet_it(tmp_path):
    q5723 = quest(load(tmp_path), 5723)

    assert q5723["name"] == "Testing an Enemy's Strength"
    assert q5723["met"] is False
    assert q5723["objectives"] == [
        {"kind": "kill", "entry": 11318, "name": "Ragefire Trogg", "needed": 8, "on_route": 1, "met": False,
         "packs": [{"key": 700, "count": 1}]}]


def test_a_kill_objective_is_met_when_the_route_pulls_the_needed_count(tmp_path):
    q5728 = quest(load(tmp_path), 5728)

    assert [(o["entry"], o["on_route"], o["met"], o["packs"]) for o in q5728["objectives"]] == [
        (11519, 1, True, []), (11518, 0, False, [{"key": 800, "count": 1}])]
    assert q5728["met"] is False


def test_an_item_objective_counts_the_mobs_on_the_route_that_drop_it(tmp_path):
    q5725 = quest(load(tmp_path), 5725)

    assert q5725["objectives"] == [
        {"kind": "item", "item": 14395, "name": "Spells of Shadow",
         "dropped_by": [{"entry": 11319, "name": "Ragefire Shaman"}, {"entry": 11322, "name": "11322"}],
         "needed": 1, "on_route": 1, "met": True, "packs": [{"key": 700, "count": 1}]}]
    assert q5725["met"] is True


def test_a_visit_point_is_met_when_the_route_line_passes_within_5_yards(tmp_path):
    loaded = load(tmp_path)

    assert quest(loaded, 5722)["objectives"] == [
        {"kind": "visit", "name": "Maur Grimtotem", "x": 105.5, "y": -45.0, "z": -12.0, "met": True, "packs": []}]
    assert quest(loaded, 5724)["met"] is False
    assert quest(loaded, 5724)["objectives"][0]["name"] == "Lost Satchel"


def test_each_pack_that_holds_a_quest_mob_has_a_badge_with_the_quest_and_the_count_in_that_pack(tmp_path):
    badges = load(tmp_path)["quest_badges"]

    assert [(b["pack"], b["quest"], b["count"], b["needed"], b["text"]) for b in badges] == [
        (501, 5723, 1, 8, "5723 Testing an Enemy's Strength: 1 of 8 Ragefire Trogg here"),
        (700, 5723, 1, 8, "5723 Testing an Enemy's Strength: 1 of 8 Ragefire Trogg here"),
        (501, 5725, 1, 1, "5725 The Power to Destroy...: 1 Ragefire Shaman here drop Spells of Shadow (1 needed)"),
        (700, 5725, 1, 1, "5725 The Power to Destroy...: 1 Ragefire Shaman here drop Spells of Shadow (1 needed)"),
        (600, 5728, 1, 1, "5728 Hidden Enemies: 1 of 1 Bazzalan here"),
        (800, 5728, 1, 1, "5728 Hidden Enemies: 1 of 1 Jergosh the Invoker here"),
    ]


def test_without_an_export_file_the_map_has_no_quests(tmp_path):
    write_tile(tmp_path / "mmaps", 389)
    write_route(tmp_path / "routes")
    settings = map_web.Settings(navmesh=tmp_path / "mmaps", routes=tmp_path / "routes", exports=tmp_path / "exports")
    loaded = TestClient(map_web.create_app(settings)).get("/api/maps/fixture_cave").json()

    assert (loaded["quests"], loaded["quest_badges"]) == ([], [])


def test_the_page_part_is_served(tmp_path):
    settings = map_web.Settings(navmesh=tmp_path, routes=tmp_path, exports=tmp_path)
    client = TestClient(map_web.create_app(settings))

    assert "map_quests.js" in client.get("/").text
    assert client.get("/map_quests.js").status_code == 200
