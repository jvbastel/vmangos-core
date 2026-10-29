import copy
from pathlib import Path

import pytest

import gear_data
import generate
from fixtures import creature, item, loot, quest, spawn, tables

BASE = {
    "levels": {"min": 1, "max": 60, "required_level_window": 6},
    "sources": {"creature_level_margin": 0, "world_drop_level_margin": 3, "include_raids": False,
                "faction_split": ["horde", "alliance"]},
    "quality": {"min": 1, "epic_min_level": 60, "blue_caps": [[1, 19, 0], [20, 39, 2], [40, 54, 4], [55, 60, 6]]},
    "pool": {"size": 10},
    "roles": {
        "warrior_tank": {
            "shapes": [["sword-and-board", "mainhand", "shield"]],
            "weapons": {"mainhand": ["sword", "mace"], "ranged": ["bow"]},
            "weights": {"stamina": 1.0, "armor": 0.0, "dps": 1.0},
        },
        "rogue_melee": {
            "shapes": [["dual-wield", "mainhand", "offhand"]],
            "weapons": {"mainhand": ["sword"], "offhand": ["sword", "dagger"]},
            "weights": {"agility": 1.0, "dps": 1.0},
        },
        "mage_ranged": {
            "shapes": [["staff", "twohand"], ["one-hand-and-off-hand", "mainhand", "holdable"]],
            "weapons": {"twohand": ["staff"], "mainhand": ["dagger"], "ranged": ["wand"]},
            "weights": {"intellect": 1.0, "wand_dps": 1.0},
        },
    },
}
STAMINA, AGILITY, INTELLECT = 7, 3, 5


def settings(**changes):
    data = copy.deepcopy(BASE)
    for path, value in changes.items():
        section, key = path.split("__")
        data[section][key] = value
    return generate.Settings.from_dict(data)


def quest_world(*items, level=10):
    """Each item is the reward of its own quest of the given level."""
    return gear_data.build_world(tables(
        items=list(items),
        quests=[quest(1000 + i, [row["entry"]], level=level) for i, row in enumerate(items)]))


def lines(world, role="warrior_tank", bans=(), **changes):
    return generate.build_lines(world, settings(**changes), role, set(bans))


def by_entry(found):
    return {line.entry: line for line in found}


def test_an_item_counts_from_its_required_level_to_six_levels_above_it():
    world = quest_world(item(100, required_level=12, stats={STAMINA: 5}), level=10)

    line = by_entry(lines(world))[100]
    assert (line.slot, line.min_level, line.max_level, line.min_patch) == ("chest", 12, 18, 0)
    assert line.score == pytest.approx(5.0)


def test_an_item_counts_only_from_the_level_of_its_source():
    world = quest_world(item(100, required_level=12, stats={STAMINA: 5}), level=15)

    assert by_entry(lines(world))[100].min_level == 15


def test_the_window_comes_from_the_settings():
    world = quest_world(item(100, required_level=12, stats={STAMINA: 5}))

    line = by_entry(lines(world, levels__required_level_window=3))[100]
    assert line.max_level == 15


def test_items_without_a_source_are_not_written():
    world = gear_data.build_world(tables(items=[item(100, stats={STAMINA: 5})]))

    assert lines(world) == []


def test_an_item_with_a_score_of_zero_never_enters_a_pool():
    world = quest_world(item(100, stats={INTELLECT: 5}), item(101, stats={STAMINA: 1}))

    assert set(by_entry(lines(world))) == {101}


def test_excluded_items_are_not_written():
    world = quest_world(
        item(100, stats={STAMINA: 5}, required_honor_rank=3),
        item(101, stats={STAMINA: 5}, required_reputation_faction=59),
        item(102, stats={STAMINA: 5}, extra_flags=4),
        item(103, stats={STAMINA: 5}, random_property=12),
        item(104, stats={STAMINA: 5}, quality=0),
        item(105, stats={STAMINA: 5}, allowable_class=1 << 7),
        item(106, stats={STAMINA: 5}, allowable_race=1),
        item(107, stats={STAMINA: 5}))

    horde = [line for line in lines(world) if line.factions != "alliance"]
    assert set(by_entry(horde)) == {107}


def test_the_ban_file_removes_items_from_every_pool():
    world = quest_world(item(100, stats={STAMINA: 5}, item_class=2, subclass=7, inventory_type=13, dmg=(5, 7), delay=2000),
                        item(101, stats={STAMINA: 5, AGILITY: 5}, item_class=2, subclass=7, inventory_type=13,
                             dmg=(5, 7), delay=2000))

    assert set(by_entry(lines(world, "warrior_tank"))) == {100, 101}
    assert set(by_entry(lines(world, "warrior_tank", bans={100}))) == {101}
    assert {line.entry for line in lines(world, "rogue_melee", bans={100})} == {101}


def test_no_blue_items_below_level_20_and_at_most_two_up_to_level_39():
    blues = [item(200 + i, required_level=18, quality=3, stats={STAMINA: 10 + i}, inventory_type=type_)
             for i, type_ in enumerate([1, 3, 5])]
    world = quest_world(*blues, level=10)

    found = by_entry(lines(world))
    # Level 18 to 24 for each, but blues only from level 20. The best two
    # (210 stamina is entry 202) are kept; the third has no room.
    assert sorted(found) == [201, 202]
    assert all((line.min_level, line.max_level) == (20, 24) for line in found.values())


def test_a_lower_blue_cap_is_a_setting():
    blues = [item(200 + i, required_level=22, quality=3, stats={STAMINA: 10 + i}, inventory_type=type_)
             for i, type_ in enumerate([1, 3, 5])]
    world = quest_world(*blues, level=10)

    found = by_entry(lines(world, quality__blue_caps=[[1, 60, 1]]))
    assert sorted(found) == [202]


def test_epic_items_count_only_at_level_60():
    world = quest_world(item(300, required_level=56, quality=4, stats={STAMINA: 30}),
                        item(301, required_level=50, quality=4, stats={STAMINA: 30}), level=50)

    found = by_entry(lines(world))
    assert (found[300].min_level, found[300].max_level) == (60, 60)
    assert 301 not in found


def test_the_warrior_tank_uses_a_one_handed_weapon_and_a_shield():
    world = quest_world(
        item(100, item_class=2, subclass=7, inventory_type=13, dmg=(5, 7), delay=2000),   # Sword
        item(101, item_class=2, subclass=8, inventory_type=17, dmg=(15, 20), delay=3000),  # Two-handed sword
        item(102, item_class=4, subclass=6, inventory_type=14, armor=300, stats={STAMINA: 2}),  # Shield
        item(103, item_class=2, subclass=15, inventory_type=13, dmg=(5, 7), delay=2000),  # Dagger: not in the settings
        item(104, item_class=2, subclass=2, inventory_type=15, dmg=(5, 7), delay=2000))   # Bow

    found = lines(world)
    assert {(line.slot, line.entry) for line in found} == {("mainhand", 100), ("shield", 102), ("ranged", 104)}
    assert generate.shapes_of(settings(), "warrior_tank") == [("sword-and-board", ["mainhand", "shield"])]


def test_the_rogue_has_no_dagger_in_the_main_hand_unless_the_settings_allow_it():
    world = quest_world(
        item(100, item_class=2, subclass=7, inventory_type=13, dmg=(5, 7), delay=2000),   # Sword
        item(101, item_class=2, subclass=15, inventory_type=13, dmg=(5, 7), delay=2000))  # Dagger

    assert {(line.slot, line.entry) for line in lines(world, "rogue_melee")} == {
        ("mainhand", 100), ("offhand", 100), ("offhand", 101)}

    data = copy.deepcopy(BASE)
    data["roles"]["rogue_melee"]["weapons"]["mainhand"] = ["sword", "dagger"]
    found = generate.build_lines(world, generate.Settings.from_dict(data), "rogue_melee", set())
    assert ("mainhand", 101) in {(line.slot, line.entry) for line in found}


def test_casters_use_a_staff_or_a_one_handed_weapon_with_an_off_hand_item_and_a_wand():
    world = quest_world(
        item(100, item_class=2, subclass=10, inventory_type=17, stats={INTELLECT: 4}, dmg=(5, 7), delay=3000),  # Staff
        item(101, item_class=2, subclass=15, inventory_type=13, stats={INTELLECT: 2}, dmg=(5, 7), delay=2000),  # Dagger
        item(102, item_class=4, subclass=0, inventory_type=23, stats={INTELLECT: 3}),                          # Off-hand item
        item(103, item_class=2, subclass=19, inventory_type=26, dmg=(5, 7), delay=1500),                       # Wand
        item(104, item_class=2, subclass=8, inventory_type=17, stats={INTELLECT: 9}, dmg=(5, 7), delay=3000),  # Two-handed sword
        item(105, item_class=2, subclass=2, inventory_type=15, stats={INTELLECT: 9}, dmg=(5, 7), delay=3000))  # Bow

    assert {(line.slot, line.entry) for line in lines(world, "mage_ranged")} == {
        ("twohand", 100), ("mainhand", 101), ("holdable", 102), ("ranged", 103)}


def test_a_class_wears_only_its_armor_and_plate_from_level_40():
    world = quest_world(
        item(100, subclass=4, required_level=38, stats={STAMINA: 5}),  # Plate
        item(101, subclass=4, required_level=38, stats={STAMINA: 5}, inventory_type=1),
        item(102, subclass=4, required_level=38, stats={INTELLECT: 5}, inventory_type=1), level=30)

    found = by_entry(lines(world))
    assert (found[100].min_level, found[100].max_level) == (40, 44)
    assert 102 not in by_entry(lines(world, "mage_ranged"))


def test_items_that_are_never_in_the_top_of_a_pool_are_not_written():
    rings = [item(400 + i, inventory_type=11, item_class=4, subclass=0, stats={STAMINA: 1 + i}) for i in range(12)]
    world = quest_world(*rings)

    assert sorted(by_entry(lines(world, pool__size=10))) == list(range(402, 412))


def test_a_later_patch_with_an_earlier_source_gets_its_own_line():
    world = gear_data.build_world(tables(
        items=[item(100, required_level=10, stats={STAMINA: 5})],
        quests=[quest(1, [100], level=16, patch=0), quest(2, [100], level=12, patch=4)]))

    found = sorted((line.min_patch, line.min_level) for line in lines(world))
    assert found == [(0, 16), (4, 12)]


def test_an_item_that_goes_away_in_a_later_patch_is_not_written():
    world = gear_data.build_world(tables(
        items=[item(100, stats={STAMINA: 5})],
        creatures=[creature(500, level=10, loot_id=500)],
        spawns=[spawn(500)],
        creature_loot=[loot(500, 100, patch_max=7)]))

    assert lines(world) == []


def test_the_template_text_has_the_template_line_the_shapes_and_the_items():
    world = quest_world(item(100, name="Sturdy Vest", stats={STAMINA: 5}),
                        item(101, name="Short Sword", item_class=2, subclass=7, inventory_type=13, dmg=(5, 7),
                             delay=2000))

    text = generate.render(world, settings(), "warrior_tank", lines(world))
    body = [line for line in text.splitlines() if line and not line.startswith("#")]
    assert body == [
        "template warrior_tank warrior tank generated",
        "shape sword-and-board mainhand shield",
        "item chest 100 10-16 0 5.00  # Sturdy Vest, green, quest",
        "item mainhand 101 10-16 0 3.00  # Short Sword, green, quest",
    ]


def test_the_settings_file_has_every_version_1_class_and_role():
    real = generate.Settings.load(Path(generate.__file__).with_name("settings.toml"))

    assert set(real.roles) >= {"warrior_tank", "priest_healer", "rogue_melee", "warlock_ranged", "mage_ranged"}


def test_a_role_that_uses_a_weapon_its_class_cannot_use_is_refused():
    data = copy.deepcopy(BASE)
    data["roles"]["mage_ranged"]["weapons"]["mainhand"] = ["mace"]

    with pytest.raises(ValueError, match="mage cannot use mace"):
        generate.Settings.from_dict(data)


def test_a_role_can_keep_to_its_armor_types():
    world = quest_world(item(100, subclass=1, stats={AGILITY: 5}),   # Cloth
                        item(101, subclass=2, stats={AGILITY: 3}))   # Leather
    data = copy.deepcopy(BASE)
    data["roles"]["rogue_melee"]["armor"] = ["leather"]

    found = generate.build_lines(world, generate.Settings.from_dict(data), "rogue_melee", set())
    assert {line.entry for line in found} == {101}
    assert {line.entry for line in lines(world, "rogue_melee")} == {100, 101}


def test_a_slot_to_fill_keeps_its_best_item_until_the_next_item_comes():
    # The tank pulls with its ranged weapon at every level.
    world = quest_world(
        item(100, required_level=10, item_class=2, subclass=2, inventory_type=15, dmg=(5, 7), delay=2000),
        item(101, required_level=30, item_class=2, subclass=2, inventory_type=15, dmg=(15, 20), delay=2000))
    role = copy.deepcopy(BASE["roles"]["warrior_tank"])
    role["fill"] = ["ranged"]

    found = by_entry(lines(world, roles__warrior_tank=role))
    assert (found[100].min_level, found[100].max_level) == (10, 29)
    assert (found[101].min_level, found[101].max_level) == (30, 60)


def test_a_slot_without_fill_keeps_the_window():
    world = quest_world(item(100, required_level=10, item_class=2, subclass=2, inventory_type=15, dmg=(5, 7), delay=2000))

    assert by_entry(lines(world))[100].max_level == 16


def test_a_slot_to_fill_must_be_a_slot_of_the_template():
    role = copy.deepcopy(BASE["roles"]["warrior_tank"])
    role["fill"] = ["nose"]
    with pytest.raises(ValueError):
        settings(roles__warrior_tank=role)


# Ticket #190: one template holds the items of both factions, each line marked.
HORDE_ONLY, ALLIANCE_ONLY = 1 << 1, 1 << 0  # Orc, Human.


def factions(found):
    return {line.entry: line.factions for line in found}


def test_each_line_is_marked_with_the_factions_that_can_use_and_get_the_item():
    world = gear_data.build_world(tables(
        items=[item(100, stats={STAMINA: 5}, allowable_race=HORDE_ONLY),
               item(101, stats={STAMINA: 5}, allowable_race=ALLIANCE_ONLY),
               item(102, stats={STAMINA: 5}),
               item(103, stats={STAMINA: 5})],
        quests=[quest(1, [100, 101, 102]), quest(2, [103], races=ALLIANCE_ONLY | (1 << 2))]))

    assert factions(lines(world)) == {100: "horde", 101: "alliance", 102: "both", 103: "alliance"}


def test_a_quest_for_horde_races_only_marks_its_reward_horde():
    world = gear_data.build_world(tables(
        items=[item(100, stats={STAMINA: 5})], quests=[quest(1, [100], races=HORDE_ONLY)]))

    assert factions(lines(world)) == {100: "horde"}


def test_a_line_for_one_faction_ends_with_the_faction_word():
    world = gear_data.build_world(tables(
        items=[item(100, name="Orc Vest", stats={STAMINA: 5}, allowable_race=HORDE_ONLY),
               item(101, name="Human Vest", stats={STAMINA: 4}, allowable_race=ALLIANCE_ONLY),
               item(102, name="Plain Vest", stats={STAMINA: 3})],
        quests=[quest(1, [100, 101, 102])]))

    body = [line for line in generate.render(world, settings(), "warrior_tank", lines(world)).splitlines()
            if line.startswith("item")]
    assert body == [
        "item chest 100 10-16 0 5.00 horde  # Orc Vest, green, quest",
        "item chest 101 10-16 0 4.00 alliance  # Human Vest, green, quest",
        "item chest 102 10-16 0 3.00  # Plain Vest, green, quest",
    ]


def test_the_alliance_items_never_change_the_horde_lines():
    # Eleven Alliance chests that score better than every Horde chest, and
    # three blue Alliance chests: without a pool and blue caps per faction they
    # would push the Horde items out of the top of the pool and the blue cap.
    def chests(first, count, races, quality=2, best=50):
        return [item(first + i, stats={STAMINA: best - i}, allowable_race=races, quality=quality, required_level=25)
                for i in range(count)]
    horde = chests(100, 12, HORDE_ONLY, best=30) + chests(150, 3, HORDE_ONLY, quality=3, best=20)
    alliance = chests(200, 11, ALLIANCE_ONLY) + chests(250, 3, ALLIANCE_ONLY, quality=3, best=40)
    horde_world = quest_world(*horde, level=25)
    both_world = quest_world(*(horde + alliance), level=25)

    before = lines(horde_world)
    after = [line for line in lines(both_world) if line.factions != "alliance"]
    assert [(line.slot, line.entry, line.min_level, line.max_level, line.min_patch, line.score) for line in after] == \
           [(line.slot, line.entry, line.min_level, line.max_level, line.min_patch, line.score) for line in before]
