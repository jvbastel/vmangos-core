import pytest

import gear_data
import builder
from fixtures import item, tables


def world_of(*items, **other):
    return gear_data.build_world(tables(items=list(items), **other))


def test_the_paste_box_reads_item_ids_and_wowhead_links():
    text = """19019
    https://www.wowhead.com/classic/item=6448/tail-spike
    https://classic.wowhead.com/item=2042, 2043 ;garbage
    http://www.wowhead.com/?item=1201"""

    assert builder.read_paste(text) == [19019, 6448, 2042, 2043, 1201]


def test_the_paste_box_checks_that_each_entry_is_a_gear_item_of_the_item_data():
    world = world_of(item(100, name="Tail Spike"), entries=[{"entry": 100}, {"entry": 2589}])

    checked = builder.check_paste([100, 2589, 99999], world)

    assert [(c.entry, c.ok) for c in checked] == [(100, True), (2589, False), (99999, False)]
    assert checked[0].item.name == "Tail Spike"
    assert "not a gear item" in checked[1].problem
    assert "no item" in checked[2].problem


STAMINA, INTELLECT = 7, 5
CLOTH, PLATE = 1, 4
CHEST, ONEHAND, HEAD = 5, 13, 1


def entries(found):
    return [f.item.entry for f in found]


def test_the_search_of_a_slot_sorts_by_score_and_shows_stats_and_sources():
    world = world_of(
        item(100, stats={STAMINA: 5}), item(101, stats={STAMINA: 9}), item(102, inventory_type=HEAD),
        item(103, item_class=2, subclass=7, inventory_type=ONEHAND, stats={STAMINA: 1}),
        vendors=[{"creature": 800, "item": 100}],
        creatures=[{"entry": 800, "patch": 0, "name": "Vendor", "level_min": 30, "loot_id": 0}],
        spawns=[{"id": 800, "map": 1, "patch_min": 0, "patch_max": 10}])

    found = builder.search(world, "chest", builder.Filters(), weights={"stamina": 2.0})

    assert entries(found) == [101, 100]
    assert found[0].score == 18.0
    assert found[1].item.stats == {"stamina": 5}
    assert [s.kind for s in found[1].sources] == ["vendor"]
    assert entries(builder.search(world, "offhand", builder.Filters(), weights={})) == [103]


def test_the_search_filters_by_level_quality_source_patch_and_armor_type():
    world = world_of(
        item(100, required_level=10, quality=2), item(101, required_level=20, quality=2),
        item(102, required_level=10, quality=3), item(103, required_level=10, patch=4),
        item(104, required_level=10, subclass=PLATE),
        crafts=[{"item": 104, "name": "Make plate"}, {"item": 100, "name": "Make cloth"}])

    def search(**filters):
        return sorted(entries(builder.search(world, "chest", builder.Filters(**filters), weights={})))

    assert search(level_min=15, level_max=25) == [101]
    assert search(qualities={3}) == [102]
    assert search(source_kinds={"craft"}) == [100, 104]
    assert search(source_kinds={"none"}) == [101, 102, 103]
    assert search(patch=2) == [100, 101, 102, 104]
    assert search(armor_types={"plate"}) == [104]
    assert search(class_name="mage") == [100, 101, 102, 103]
    assert search(text="item") == [100, 101, 102, 103, 104]


TWOHAND, SHIELD, FINGER = 17, 14, 11


def hand(*items, name="grukk_dungeon", class_name="warrior", role="tank"):
    return builder.HandTemplate(name, class_name, role, [builder.HandItem(*line) for line in items])


def test_a_hand_made_template_is_written_and_read_in_the_gear_template_format():
    world = world_of(item(100, name="Tail  Spike #2"), item(101, inventory_type=FINGER, name="Ring"))
    template = hand(("chest", 100, 16, 20, 0), ("finger", 101, 12, 18, 3))

    text = builder.render(template, world)

    assert text.splitlines()[-3:] == [
        "template grukk_dungeon warrior tank hand",
        "item chest 100 16-20 0  # Tail Spike 2",
        "item finger 101 12-18 3  # Ring",
    ]
    assert builder.parse(text) == template


def test_the_check_finds_the_problems_that_the_core_loader_rejects():
    world = world_of(item(100), item(101), item(102, inventory_type=TWOHAND, item_class=2, subclass=1),
                     item(103, inventory_type=SHIELD, subclass=6), item(104, inventory_type=FINGER),
                     item(105, inventory_type=FINGER), item(106, inventory_type=FINGER))

    assert builder.check(hand(("chest", 100, 1, 20, 0), ("chest", 101, 21, 60, 0)), world) == []
    assert builder.check(hand(name="warrior_tank"), world) == [
        "The name warrior_tank is for a generated template. Give the hand-made template another name."]
    assert builder.check(hand(name="Bad Name"), world) == ["A template name has only a-z, 0-9, _ and -."]
    assert builder.check(hand(class_name="mage", role="tank"), world) == ["A mage cannot play the role tank."]
    assert builder.check(hand(("chest", 100, 1, 20, 0), ("chest", 101, 20, 30, 0)), world) == [
        "Two items fill the position chest at level 20 and content patch 0."]
    assert builder.check(hand(("twohand", 102, 1, 20, 0), ("shield", 103, 10, 30, 0)), world) == [
        "Two items fill the position offhand at level 10 and content patch 0."]
    assert builder.check(hand(("finger", 104, 1, 60, 0), ("finger", 105, 1, 60, 0), ("finger", 106, 5, 6, 0)),
                         world) == ["Three items fill the position finger at level 5 and content patch 0."]
    assert builder.check(hand(("head", 100, 1, 20, 0), ("chest", 999, 1, 20, 0), ("chest", 101, 30, 61, 11)),
                         world) == ["Item 100 (Item) does not fit the slot head.",
                                    "No gear item 999 in the item data.",
                                    "Levels are from 1 to 60, the lower first: 30-61.",
                                    "Content patches are from 0 to 10: 11."]


def test_the_empty_slots_are_the_levels_with_no_item():
    template = hand(("chest", 100, 1, 20, 0), ("chest", 101, 31, 60, 0), ("finger", 104, 10, 60, 0),
                    ("twohand", 102, 1, 60, 0))

    empty = builder.empty_levels(template)

    assert empty["chest"] == [(21, 30)]
    assert empty["head"] == [(1, 60)]
    assert empty["finger"] == [(1, 60)]          # Only one of the two positions is full.
    assert empty["mainhand"] == [] and empty["offhand"] == []


def test_save_writes_only_good_hand_made_templates(tmp_path):
    world = world_of(item(100))
    (tmp_path / "odd.txt").write_text("template odd warrior tank generated\n")

    path = builder.save(tmp_path, hand(("chest", 100, 1, 60, 0)), world)

    assert path == tmp_path / "grukk_dungeon.txt"
    assert builder.parse(path.read_text()) == hand(("chest", 100, 1, 60, 0))
    with pytest.raises(builder.SaveError, match="generated template"):
        builder.save(tmp_path, hand(name="warrior_tank"), world)
    with pytest.raises(builder.SaveError, match="not a hand-made template"):
        builder.save(tmp_path, hand(name="odd"), world)
    assert (tmp_path / "odd.txt").read_text() == "template odd warrior tank generated\n"
    assert not (tmp_path / "warrior_tank.txt").exists()


def test_the_hand_made_templates_of_the_folder_are_listed(tmp_path):
    (tmp_path / "warrior_tank.txt").write_text("template warrior_tank warrior tank generated\n")
    (tmp_path / "grukk.txt").write_text("template grukk warrior tank hand\n")
    (tmp_path / "README.md").write_text("")

    assert builder.hand_made_names(tmp_path) == ["grukk"]


def test_a_pasted_item_goes_to_its_slot_from_its_required_level_and_first_patch():
    world = world_of(item(100, required_level=24, patch=3),
                     item(101, item_class=2, subclass=7, inventory_type=ONEHAND, required_level=0))

    assert builder.new_line(world.items[100]) == builder.HandItem("chest", 100, 24, 60, 3)
    assert builder.new_line(world.items[101]) == builder.HandItem("mainhand", 101, 1, 60, 0)
