"""The Forever loot of Shadowfang Keep (jvbastel/WOWAI#245, spec #240).

The spec rows are in forever/spec/loot_shadowfang_keep.tsv (drops, quest rewards) and loot_shadowfang_keep_items.tsv
(items). The test runs the generator on the spec folder and reads the generated SQL and the loot list from the
outside. The expected values come from Wowhead's Forever tooltips and quest pages (read 2026-10-31,
notes/forever/wowhead-cache, notes/forever/loot-data-shadowfang_keep.md) and the stock world database (boss loot
entries, references, quest rewards). The shared row cases and refusals are tested in test_loot_rfc.py.
"""
import re

import pytest

from loot_helpers import (CREATURE_LOOT, REFERENCE_LOOT, bonus_of, changed, copied_entries, item_rows, loot_list,
                          loot_rows, new_equip_spells, quest_changes, refused, spec_drops, sql, stats_of,
                          stock_equip_spells, stock_gear)
from test_blacksmithing import spec_rows
from test_pipeline import STOCK, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

ITEMS, LOOT = "loot_shadowfang_keep_items.tsv", "loot_shadowfang_keep.tsv"
# The SFK bosses (stock creature_template; the loot entry of each is its creature entry).
RETHILGORE, RAZORCLAW, SILVERLAINE, SPRINGVALE, ODO = 3914, 3886, 3887, 4278, 4279
FENRUS, NANDOS, ARUGAL, DEATHSWORN = 4274, 3927, 4275, 3872
BOSSES = {RETHILGORE, RAZORCLAW, SILVERLAINE, SPRINGVALE, ODO, FENRUS, NANDOS, ARUGAL, DEATHSWORN}
# The stock reference loot entries of the bosses (each used by its boss alone).
REFERENCES = {30129, 30130, 30131, 30132, 30139, 30140, 30141, 30142}

# The stock boss drops with their Forever values (Wowhead): name, quality, armor (with the Forever bonus armor),
# base stats, on-equip bonuses, (damage, delay) of a weapon, resistances that are not 0.
STOCK_DROPS = {
    5254: ("Rugged Spaulders", 3, 87, {}, set(), None,
           {"arcane_res": 2, "fire_res": 2, "nature_res": 2, "frost_res": 2, "shadow_res": 2}),
    1292: ("Butcher's Cleaver", 3, 0, {"Str": 5, "Agi": 2}, set(), ((19, 36), 1700), {}),
    6226: ("Bloody Apron", 3, 42, {"Sta": 10}, {("SpellPower", 4), ("Healing", 9)}, None, {}),
    6633: ("Butcher's Slicer", 3, 0, {"Sta": 3}, {("AP", 6)}, ((30, 57), 2900), {}),
    6321: ("Silverlaine's Family Seal", 3, 0, {"Str": 5, "Sta": 2}, set(), None, {}),
    6323: ("Baron's Scepter", 3, 0, {"Str": 2, "Sta": 3}, set(), ((28, 52), 2400), {}),
    3191: ("Arced War Axe", 3, 0, {"Str": 9, "Sta": 9}, set(), ((57, 86), 3300), {}),
    6320: ("Commander's Crest", 3, 585, {"Str": 5, "Sta": 3}, set(), None, {}),
    6318: ("Odo's Ley Staff", 3, 0, {"Sta": 5, "Spi": 12}, {("SpellPower", 18), ("Healing", 35)}, ((37, 56), 2900), {}),
    6319: ("Girdle of the Blindwatcher", 3, 54, {"Sta": 5}, {("SpellPower", 7)}, None, {}),
    3230: ("Black Wolf Bracers", 3, 63, {"Sta": 7}, {("AP", 4)}, None, {}),
    6340: ("Fenrus' Hide", 3, 22, {"Agi": 4, "Sta": 6}, set(), None, {}),
    3748: ("Feline Mantle", 3, 34, {"Agi": 2, "Int": 8}, {("SpellPower", 4)}, None, {}),
    6314: ("Wolfmaster Cape", 3, 22, {"Sta": 3}, {("AP", 10), ("BeastSlaying", 15)}, None, {}),
    6220: ("Meteor Shard", 3, 0, {}, set(), ((22, 42), 1800), {"fire_res": 3}),
    6324: ("Robes of Arugal", 3, 46, {"Sta": 5, "Spi": 6}, {("SpellPower", 6), ("Healing", 13)}, None, {"shadow_res": 8}),
    6392: ("Belt of Arugal", 3, 26, {"Agi": 2, "Int": 3, "Spi": 5}, {("SpellPower", 9)}, None, {}),
    6641: ("Haunting Blade", 3, 0, {}, {("SpellPower", 11), ("Healing", 22)}, ((68, 102), 3800), {}),
    6642: ("Phantom Armor", 3, 208, {"Str": 3, "Sta": 12, "Spi": 5}, set(), None, {}),
}
# The stock quest rewards with their Forever values: quality, base stats, on-equip bonuses.
QUEST_ITEMS = {
    3324: ("Ghostly Mantle", 2, {"Spi": 6}, {("SpellPower", 2), ("Healing", 4)}),
    4534: ("Steel-clasped Bracers", 2, {"Sta": 4, "Spi": 4}, set()),
    6335: ("Grizzled Boots", 2, {"Sta": 7}, set()),
    6414: ("Seal of Sylvanas", 3, {"Sta": 8}, {("AP", 6)}),
    6898: ("Orb of Soran'ruk", 3, {}, {("FireDamage", 9), ("ShadowDamage", 9)}),
    15109: ("Staff of Soran'ruk", 3, {"Sta": 14}, {("FireDamage", 34), ("ShadowDamage", 34)}),
}

# The new items (Wowhead Forever tooltips): entry: (name, Forever item, (class, subclass, slot) of Item.db2, quality,
# base stats, on-equip bonuses as (bonus, value), (damage, delay) of a weapon, armor).
NEW_ITEMS = {
    71300: ("Cell Keeper's Claws", 273456, (2, 13, 21), 3, {"Agi": 2}, {("AP", 6)}, ((25, 48), 2500), 0),
    71301: ("Sorcerer Collar", 273457, (4, 0, 2), 3, {"Int": 2, "Spi": 2}, {("SilenceRes", 5)}, None, 0),
    71302: ("Blade of Silverlaine", 273637, (2, 7, 21), 3, {}, {("SpellPower", 28)}, ((17, 33), 2300), 0),
    71303: ("Worgenbane Talisman", 273643, (4, 0, 12), 3, {}, set(), None, 0),
    71304: ("Blindwatcher's Sight", 273645, (4, 1, 1), 3, {}, set(), None, 35),
    # 143 armor and -100 bonus armor.
    71305: ("Half-Eaten Boots", 273646, (4, 3, 8), 3, {"Str": 8}, set(), None, 43),
    71306: ("Worgpelt Leggings", 273647, (4, 2, 7), 3, {"Agi": 10, "Str": 5}, set(), None, 84),
    71307: ("Tanned Shoulderpads", 270023, (4, 2, 3), 2, {"Str": 3, "Int": 6}, set(), None, 67),
    71308: ("Bronzed Shoulderguards", 270024, (4, 3, 3), 2, {"Sta": 4, "Spi": 5}, set(), None, 142),
    71309: ("Tattered Mittens", 270030, (4, 1, 10), 2, {"Sta": 6}, {("SpellPower", 4), ("Healing", 7)}, None, 26),
}
# The new boss drops of the spec (Wowhead Forever "Dropped by" and "Drop Chance"): (creature loot entry, item): chance.
DROPS = spec_drops(LOOT)
# Quest 1013 The Book of Ur: stock choices 6335 and 4534, Forever adds Tattered Mittens. Quest 1098 Deathstalkers in
# Shadowfang: stock gives Ghostly Mantle (3324) as its fixed reward and has no choices; Forever offers Ghostly Mantle,
# Tanned Shoulderpads and Bronzed Shoulderguards as three choices and no fixed reward.
BOOK_OF_UR, DEATHSTALKERS, GHOSTLY_MANTLE = 1013, 1098, 3324
# The Forever effects that 1.12 has no stock spell for (or a stock spell of another value), and Unique-Equipped.
LEFT_OUT = {6321, 6323, 6319, 3748, 6220, 6898, 71300, 71303, 71304}


def stock_item_spells(text, name, entry):
    return stock_equip_spells(text, name, entry)


def equip_bonuses(spells, ids):
    return {bonus_of(spells, spell) for spell in ids}


def test_the_new_items_are_the_new_rows_of_the_sfk_file():
    assert {int(r["entry"]) for r in spec_rows(ITEMS) if r["kind"] != "stock"} == set(NEW_ITEMS)


# --- Items.

def test_the_stock_boss_drops_get_their_forever_stats(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    text, rows = sql(out), item_rows(out, "forever_item_stock")
    for entry, (name, quality, armor, stats, bonuses, weapon, resistances) in STOCK_DROPS.items():
        row = rows[entry]
        assert (row["name"], int(row["quality"]), int(row["armor"]), stats_of(row)) == (name, quality, armor, stats), name
        assert equip_bonuses(spells, stock_item_spells(text, name, entry)) == bonuses, name
        assert {c: int(row[c]) for c in row if c.endswith("_res") and row[c] != "0"} == resistances, name
        if weapon:
            assert ((int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])) == weapon, name


def test_the_sfk_quest_rewards_get_their_forever_stats(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    text, rows = sql(out), item_rows(out, "forever_item_stock")
    for entry, (name, quality, stats, bonuses) in QUEST_ITEMS.items():
        row = rows[entry]
        assert (row["name"], int(row["quality"]), stats_of(row)) == (name, quality, stats), name
        assert equip_bonuses(spells, stock_item_spells(text, name, entry)) == bonuses, name


def test_a_new_item_copies_a_stock_item_of_its_class_subclass_and_slot_and_takes_its_display(out):
    text, gear = sql(out), stock_gear()
    displays, _ = read_dbc(STOCK / "ItemDisplayInfo.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, kind, quality, _, _, _, armor) in NEW_ITEMS.items():
        source = int(re.search(r"^-- Item %s \(%d\), from item (\d+)\.$" % (re.escape(name), entry), text, re.M).group(1))
        assert gear[source][:3] == kind, name
        assert int(rows[entry]["display_id"]) == gear[source][3] and gear[source][3] in displays
        assert (rows[entry]["name"], int(rows[entry]["quality"]), int(rows[entry]["armor"])) == (name, quality, armor)


def test_the_new_items_have_their_forever_stats_as_stock_equip_spells(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, _, _, stats, bonuses, weapon, _) in NEW_ITEMS.items():
        row = rows[entry]
        assert stats_of(row) == stats, name
        equip = {bonus_of(spells, spell) for spell in new_equip_spells(row)}
        assert equip == bonuses, name
        if weapon:
            assert ((int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])) == weapon, name


# --- Boss drops.

def test_each_new_drop_is_a_row_outside_the_stock_groups_on_a_copy_of_the_boss_loot(out):
    text = sql(out)
    rows = {key: value for key, value in loot_rows(text).items() if key[0] in BOSSES}
    # Group 0 (no equal-chance group), one item, patch 0 to 10.
    assert rows == {key: (chance, 0, 1, 1, 0, 10) for key, chance in DROPS.items()}
    assert copied_entries(text) & BOSSES == {RETHILGORE, SILVERLAINE, SPRINGVALE, ODO, FENRUS, NANDOS}
    for entry, item in DROPS:
        copy = text.index("INSERT INTO `%s` SELECT * FROM `creature_loot_template` WHERE `entry`=%d;" % (CREATURE_LOOT, entry))
        assert text.index("VALUES (%d, %d, " % (entry, item)) > copy


def test_the_stock_drops_keep_their_stock_chance_and_group(out):
    # The stock equal-chance groups are in the boss references, and Rugged Spaulders is a stock row (100 %) of
    # Rethilgore's creature loot: no Forever row changes them. The copied creature loot keeps every stock row.
    text = sql(out)
    assert not REFERENCES & copied_entries(text, REFERENCE_LOOT)
    assert not {key for key in loot_rows(text, REFERENCE_LOOT) if key[0] in REFERENCES}
    assert not {key for key in loot_rows(text) if key[1] in STOCK_DROPS}


# --- Quest rewards.

def test_the_book_of_ur_offers_tattered_mittens_as_its_third_choice(out):
    assert quest_changes(out)[BOOK_OF_UR] == {"RewChoiceItemId3": "71309", "RewChoiceItemCount3": "1"}


def test_deathstalkers_in_shadowfang_offers_three_choices_and_no_fixed_reward(out):
    text = sql(out)
    assert quest_changes(out)[DEATHSTALKERS] == {
        "RewChoiceItemId1": "3324", "RewChoiceItemCount1": "1", "RewChoiceItemId2": "71307", "RewChoiceItemCount2": "1",
        "RewChoiceItemId3": "71308", "RewChoiceItemCount3": "1"}
    block = text[text.index("-- Quest %d " % DEATHSTALKERS):]
    block = block[:block.index("DROP TEMPORARY TABLE")]
    # The stock rows must give Ghostly Mantle as a fixed reward, or the SQL fails; then the fixed slot is emptied.
    any_slot = " OR ".join("`RewItemId%d`=%d" % (k, GHOSTLY_MANTLE) for k in range(1, 5))
    assert ("SELECT IF((SELECT COUNT(*) FROM `forever_quest_new` WHERE NOT (%s))=0, 0, (SELECT 1 UNION SELECT 2)) "
            "INTO @forever_fixed;" % any_slot) in block
    clear = ", ".join("`RewItemCount{k}`=IF(`RewItemId{k}`={i}, 0, `RewItemCount{k}`), "
                      "`RewItemId{k}`=IF(`RewItemId{k}`={i}, 0, `RewItemId{k}`)".format(k=k, i=GHOSTLY_MANTLE)
                      for k in range(1, 5))
    assert "UPDATE `forever_quest_new` SET %s;" % clear in block


def test_a_fixed_reward_that_is_no_stock_item_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "reward\t3324\tGhostly Mantle", "reward\t79991\tGhostly Mantle"))
    assert "item 79991 is no item" in error


# --- The lists.

def test_the_loot_list_names_each_left_out_sfk_effect_and_nothing_unconfirmed(out):
    ours = set(STOCK_DROPS) | set(QUEST_ITEMS) | set(NEW_ITEMS)
    listed = {key: detail for key, detail in loot_list(out).items() if int(key[2]) in ours}
    assert {key for key in listed if key[0] != "manual-source"} == {("left-out", "item", str(e)) for e in LEFT_OUT}
    assert "Attack Power against Humanoids" in listed[("left-out", "item", "71300")]
    assert "Worgen" in listed[("left-out", "item", "71303")]


def test_an_unconfirmed_sfk_drop_is_left_out_and_listed(tmp_path):
    out, done = changed(tmp_path, (LOOT, "1.67\t-\t-\tconfirmed", "1.67\t-\t-\tunconfirmed"))
    assert done.returncode == 0, done.stderr
    assert ("unconfirmed", "drop", "71303") in loot_list(out)
    assert (SPRINGVALE, 71303) not in loot_rows(sql(out))
