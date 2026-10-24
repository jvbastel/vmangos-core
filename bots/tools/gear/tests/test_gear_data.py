import pytest

import gear_data
from fixtures import creature, item, loot, quest, spawn, spell, tables

RULES = gear_data.SourceRules(creature_level_margin=0, world_drop_level_margin=3, include_raids=False)
WARRIOR = 1


def earliest(world, entry, patch=10, class_id=WARRIOR):
    return gear_data.earliest_level(world.sources.get(entry, []), patch, class_id, RULES)


def test_a_quest_reward_counts_from_the_quest_level_and_the_quest_patch():
    world = gear_data.build_world(tables(
        items=[item(100, required_level=10)],
        quests=[quest(1, [100], level=14, patch=3)]))

    assert earliest(world, 100, patch=3) == 14
    assert earliest(world, 100, patch=2) is None


def test_a_quest_for_alliance_races_or_another_class_is_no_source():
    world = gear_data.build_world(tables(
        items=[item(100), item(101)],
        quests=[quest(1, [100], races=1 | 4), quest(2, [101], classes=1 << 7)]))

    assert earliest(world, 100) is None
    assert earliest(world, 101) is None
    assert earliest(world, 101, class_id=8) == 10


def test_creature_loot_uses_the_creature_level_and_a_world_drop_counts_three_levels_early():
    world = gear_data.build_world(tables(
        items=[item(100), item(101)],
        creatures=[creature(500, level=15, loot_id=500)],
        spawns=[spawn(500)],
        creature_loot=[loot(500, 100), loot(500, 0, ref=9000)],
        reference_loot=[loot(9000, 101)]))

    assert earliest(world, 100) == 15
    assert earliest(world, 101) == 12
    assert {source.kind for source in world.sources[101]} == {"world"}


def test_a_creature_without_a_spawn_is_no_source():
    world = gear_data.build_world(tables(
        items=[item(100)],
        creatures=[creature(500, level=15, loot_id=500)],
        creature_loot=[loot(500, 100)]))

    assert earliest(world, 100) is None


def test_a_dungeon_drop_counts_from_the_lowest_level_of_the_creatures_with_loot_on_the_map():
    world = gear_data.build_world(tables(
        items=[item(100), item(101)],
        maps=[{"entry": 389, "map_type": 1, "name": "Ragefire Chasm"},
              {"entry": 409, "map_type": 2, "name": "Molten Core"}],
        creatures=[creature(599, level=1, loot_id=0, name="A rat"), creature(600, level=13, loot_id=600),
                   creature(601, level=16, loot_id=601), creature(700, level=60, loot_id=700)],
        spawns=[spawn(599, map_id=389), spawn(600, map_id=389), spawn(601, map_id=389), spawn(700, map_id=409)],
        creature_loot=[loot(601, 100), loot(700, 101)]))

    assert earliest(world, 100) == 13
    assert world.sources[100][0].name == "Ragefire Chasm"
    assert earliest(world, 101) is None  # Raids are off in the rules.


def test_loot_counts_only_in_the_patches_of_the_loot_row_and_the_spawn():
    world = gear_data.build_world(tables(
        items=[item(100)],
        creatures=[creature(500, level=15, loot_id=500)],
        spawns=[spawn(500, patch_min=2)],
        creature_loot=[loot(500, 100, patch_min=0, patch_max=6)]))

    assert [earliest(world, 100, patch) for patch in range(11)] == [None, None] + [15] * 5 + [None] * 4


def test_vendor_and_crafted_items_count_at_any_level():
    world = gear_data.build_world(tables(
        items=[item(100), item(101)],
        creatures=[creature(800, level=30, loot_id=0)],
        spawns=[spawn(800)],
        vendors=[{"creature": 800, "item": 100}],
        crafts=[{"item": 101, "name": "Brown Linen Robe"}]))

    assert earliest(world, 100) == 0
    assert earliest(world, 101) == 0


def test_the_patches_of_an_item_follow_its_rows_and_the_forbidden_items():
    world = gear_data.build_world(tables(
        items=[item(100, patch=2), item(101), item(102), item(103), item(103, patch=4, inventory_type=20)],
        forbidden=[{"entry": 101, "patch": 6, "after_or_before": 1},
                   {"entry": 102, "patch": 8, "after_or_before": 0}]))

    assert world.items[100].patches == frozenset(range(2, 11))
    assert world.items[101].patches == frozenset(range(7, 11))
    assert world.items[102].patches == frozenset(range(0, 8))
    # A row that changes the slot: the item counts only from that row on.
    assert world.items[103].patches == frozenset(range(4, 11))
    assert world.items[103].slot == "chest"


def test_item_stats_include_the_equip_spells_and_weapon_damage_per_second():
    world = gear_data.build_world(tables(
        items=[item(100, item_class=2, subclass=10, inventory_type=17, stats={5: 6, 7: 3},
                    dmg=(20, 40), delay=3000, spells=(4000, 4001, 4002))],
        spells=[spell(4000, 13, 7, misc=126), spell(4001, 85, 2), spell(4002, 13, 5, misc=16)]))

    stats = world.items[100].stats
    assert stats["intellect"] == 6
    assert stats["stamina"] == 3
    assert stats["spell_damage"] == 7
    assert stats["healing"] == 7  # Damage and healing done by magical spells.
    assert stats["mp5"] == 2
    assert stats["frost_damage"] == 5
    assert world.items[100].dps == pytest.approx(10.0)
    assert world.items[100].slot == "twohand"
    assert world.items[100].weapon_type == "staff"


def test_score_is_the_weighted_stats_plus_armor_plus_damage_per_second():
    world = gear_data.build_world(tables(
        items=[item(100, stats={7: 4, 4: 2}, armor=50),
               item(101, item_class=2, subclass=19, inventory_type=26, dmg=(10, 20), delay=1500)]))
    weights = {"stamina": 1.0, "strength": 0.5, "armor": 0.1, "dps": 2.0, "wand_dps": 0.5}

    assert gear_data.score(world.items[100], weights) == pytest.approx(10.0)
    assert gear_data.score(world.items[101], weights) == pytest.approx(5.0)


def test_the_ban_file_lists_entries(tmp_path):
    path = tmp_path / "bans.txt"
    path.write_text("# Banned items\nban 100   # A bad item\n\nban 200\n")

    assert gear_data.read_bans(path) == {100, 200}


def test_the_ban_file_rejects_other_lines(tmp_path):
    path = tmp_path / "bans.txt"
    path.write_text("ban 100\nunban 200\n")

    with pytest.raises(ValueError, match="line 2"):
        gear_data.read_bans(path)


def test_an_item_keeps_its_display_id_for_the_icons_of_the_builder_page():
    world = gear_data.build_world(tables(items=[item(100, display_id=7012)]))

    assert world.items[100].display_id == 7012
