from fastapi.testclient import TestClient

import builder_web
import gear_data
import generate
from fixtures import item, tables
from test_generate import settings

STAMINA = 7


def client(tmp_path, icons=None):
    world = gear_data.build_world(tables(
        items=[item(100, name="Tail Spike", stats={STAMINA: 4}, display_id=7012),
               item(101, name="Plain Robe", stats={STAMINA: 1})],
        crafts=[{"item": 100, "name": "Make spike"}],
        entries=[{"entry": 2589}]))
    app = builder_web.create_app(world, tmp_path, settings(), bans={101}, icons=icons)
    return TestClient(app)


def test_the_page_is_served(tmp_path):
    response = client(tmp_path).get("/")

    assert response.status_code == 200
    assert "Template builder" in response.text


def test_the_search_shows_score_stats_and_sources_with_the_weights_of_the_role(tmp_path):
    response = client(tmp_path).get("/api/search", params={
        "slot": "chest", "class_name": "warrior", "role": "tank", "source": ["craft", "none"]})

    found = response.json()["items"]
    assert [(i["entry"], i["score"], i["banned"]) for i in found] == [(100, 4.0, False), (101, 1.0, True)]
    assert found[0]["stats"] == {"stamina": 4}
    assert found[0]["sources"] == [{"kind": "craft", "name": "Make spike", "level": 0}]
    assert found[0]["icon"] is None
    assert found[0]["quality_name"] == "green"


def test_the_paste_box_answers_for_each_entry(tmp_path):
    response = client(tmp_path).post("/api/paste", json={"text": "100 https://www.wowhead.com/classic/item=2589\n7"})

    results = response.json()["results"]
    assert [(r["entry"], r["ok"]) for r in results] == [(100, True), (2589, False), (7, False)]
    assert results[0]["line"] == {"slot": "chest", "entry": 100, "min_level": 10, "max_level": 60, "min_patch": 0}


def test_the_page_saves_a_hand_made_template_and_shows_its_empty_slots(tmp_path):
    web = client(tmp_path)
    template = {"name": "grukk", "class_name": "warrior", "role": "tank",
                "items": [{"slot": "chest", "entry": 100, "min_level": 10, "max_level": 60, "min_patch": 0}]}

    checked = web.post("/api/check", json=template).json()
    saved = web.put("/api/templates/grukk", json=template)
    refused = web.put("/api/templates/warrior_tank", json={**template, "name": "warrior_tank"})

    assert checked["problems"] == []
    assert checked["empty"]["chest"] == [[1, 9]]
    assert checked["empty"]["head"] == [[1, 60]]
    assert saved.status_code == 200
    assert (tmp_path / "grukk.txt").read_text().splitlines()[-1] == "item chest 100 10-60 0  # Tail Spike"
    assert web.get("/api/meta").json()["templates"] == ["grukk"]
    assert web.get("/api/templates/grukk").json()["template"] == template
    assert refused.status_code == 400
    assert not (tmp_path / "warrior_tank.txt").exists()


def test_the_page_shows_icons_when_an_icon_source_has_them(tmp_path):
    class Icons:
        def url(self, item):
            return f"/icons/{item.display_id}.png" if item.display_id else None

    found = client(tmp_path, icons=Icons()).get("/api/search", params={"slot": "chest"}).json()["items"]

    assert {i["entry"]: i["icon"] for i in found} == {100: "/icons/7012.png", 101: None}
