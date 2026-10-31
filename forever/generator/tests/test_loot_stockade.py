"""The Forever loot of the Stockade (jvbastel/WOWAI#247, spec #240).

The spec rows are in forever/spec/loot_stockade.tsv (drops, quest rewards) and loot_stockade_items.tsv (items). The
test runs the generator on the spec folder and reads the generated SQL and the loot list from the outside. It looks
only at the Stockade's bosses, quests and items, so the rows of the other dungeons do not change it. The expected
values come from Wowhead's Forever tooltips and quest pages (read 2026-10-31, notes/forever/wowhead-cache) and the
stock world database (notes/forever/loot-data-stockade.md: boss loot entries, stock choices, stock displays).
"""
import re

import pytest

from test_blacksmithing import spec_rows
from test_loot_rfc import (CREATURE_LOOT, REFERENCE_LOOT, ON_EQUIP, changed, copied_entries, item_rows, loot_list,
                           loot_rows, quest_changes, refused, sql, stats_of, stock_gear)
from test_pipeline import STOCK, bonus_of, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

ITEMS, LOOT = "loot_stockade_items.tsv", "loot_stockade.tsv"
DEXTREN, KAM, BAZIL, TARGORR, HAMHOCK, BRUEGAL = 1663, 1666, 1716, 1696, 1717, 1720
BOSSES = {DEXTREN, KAM, BAZIL, TARGORR, HAMHOCK, BRUEGAL}
BLOCK = range(71500, 71600)
# The stock boss drops of Kam Deepfury (2280) and Bruegal Ironknuckle with their Forever values (Wowhead): quality,
# item level, required level, armor, base stats, on-equip bonuses, (damage, delay) of a weapon.
STOCK_DROPS = {
    2280: ("Kam's Walking Stick", 3, 29, 24, 0, {"Agi": 10, "Int": 3}, {("SpellPower", 32)}, ((39, 60), 2800)),
    2941: ("Prison Shank", 3, 28, 23, 0, {"Agi": 6, "Sta": 2}, set(), ((22, 42), 1800)),
    2942: ("Iron Knuckles", 3, 28, 23, 0, {}, set(), ((21, 39), 1700)),
    3228: ("Jimmied Handcuffs", 3, 28, 23, 92, {"Str": 3, "Sta": 8}, set(), None),
}
# The new items (Wowhead Forever tooltips): entry: (name, Forever item, (class, subclass, slot) of Item.db2, quality,
# base stats, on-equip bonuses, (damage, delay) of a weapon). A bonus with no stock value is the rounded one.
NEW_ITEMS = {
    71500: ("Executioner Mantle", 273804, (4, 2, 3), 3, {"Spi": 9}, {("AP", 10)}, None),
    71501: ("Blackrock Harness", 273805, (4, 2, 5), 3, {"Sta": 8}, {("Defense", 4), ("AP", 14)}, None),
    71502: ("Dark Horde Band", 273806, (4, 0, 11), 3, {"Sta": 6}, {("SpellPower", 6)}, None),
    71503: ("Demolition Girdle", 273807, (4, 2, 6), 3, {"Str": 8}, set(), None),
    71504: ("Bridgebreaker Bindings", 273808, (4, 1, 9), 3, {}, {("SpellPower", 4), ("Healing", 7)}, None),
    71505: ("Hamhock's Cleaver", 273809, (2, 0, 21), 3, {"Str": 4}, {("AP", 8)}, ((29, 54), 2200)),
    71506: ("Ogre Grips", 273810, (4, 3, 10), 3, {"Str": 4, "Sta": 10}, set(), None),
    71507: ("Repurposed Rack", 273811, (4, 6, 14), 3, {"Str": 6}, {("SpellPower", 7)}, None),
    71508: ("Graverobber's Shovel", 273817, (2, 5, 17), 3, {"Sta": 10}, {("AP", 12)}, ((68, 102), 3700)),
    71509: ("Boneslicer", 273819, (2, 6, 17), 3, {}, set(), ((36, 55), 2000)),
    71510: ("Nightskulker Ring", 273820, (4, 0, 11), 3, {}, set(), None),
    71511: ("Defias Jailbreakers", 273824, (4, 2, 10), 3, {"Agi": 6, "Sta": 7}, set(), None),
    71512: ("Red Wool Cloak", 273825, (4, 1, 16), 3, {}, {("SpellPower", 4), ("Healing", 7), ("Mp5", 3)}, None),
    71513: ("Debt Collector", 273827, (2, 7, 21), 3, {}, {("SpellPower", 34)}, ((21, 39), 2300)),
    71514: ("Concealed Hand Crossbow", 273829, (2, 18, 26), 3, {}, {("RangedAP", 12)}, ((24, 37), 2000)),
    71515: ("Ursine Hammer", 270027, (2, 5, 17), 2, {"Agi": 5, "Int": 5, "Spi": 5}, set(), ((47, 71), 3200)),
    71516: ("Town Clerk's Mittens", 270029, (4, 1, 10), 2, {"Int": 7}, {("SpellPower", 4)}, None),
    71517: ("Magistrate's Pantaloons", 270036, (4, 1, 7), 3, {"Int": 13}, {("SpellPower", 7)}, None),
}
# The new boss drops (Wowhead Forever "Dropped by" and "Drop Chance"): (creature loot entry, item): chance. The loot
# entry of each Stockade boss is its creature entry (stock creature_template.loot_id).
DROPS = {
    (TARGORR, 71500): 1.5, (TARGORR, 71501): 1.7, (TARGORR, 71502): 1.68,
    (KAM, 71503): 2.19, (KAM, 71504): 1.83,
    (HAMHOCK, 71505): 1.75, (HAMHOCK, 71506): 1.47, (HAMHOCK, 71507): 1.79,
    (DEXTREN, 71508): 2.0, (DEXTREN, 71509): 1.76, (DEXTREN, 71510): 1.36,
    (BAZIL, 71511): 0.98, (BAZIL, 71512): 0.92, (BAZIL, 71513): 1.23, (BAZIL, 71514): 0.94,
}
# The stock references of the bosses (stock world DB): Bruegal's equal-chance group of three blues and Kam's staff.
STOCK_REFERENCES = {30094, 30095, 30096, 30097, 30098, 30099, 30100, 30101}
# Quests (Wowhead Forever quest pages): the stock choices fill slots 1 and 2; Forever adds a third choice.
CRIME, FURY, WHAT_COMES = 377, 378, 386
QUEST_CHANGES = {
    CRIME: {"RewChoiceItemId3": "71516", "RewChoiceItemCount3": "1"},
    FURY: {"RewChoiceItemId3": "71517", "RewChoiceItemCount3": "1"},
    WHAT_COMES: {"RewChoiceItemId3": "71515", "RewChoiceItemCount3": "1"},
}
STOCK_CHOICES = {CRIME: (2033, 2906), FURY: (3562, 1264), WHAT_COMES: (3400, 1317)}
# The stock reward items of the three quests with their Forever values (Wowhead): quality, item level, armor, base
# stats, on-equip bonuses, (damage, delay) of a weapon.
QUEST_ITEMS = {
    1264: ("Headbasher", 3, 29, 0, {"Str": 9, "Sta": 5, "Int": 10}, set(), ((43, 66), 2300)),
    1317: ("Hardened Root Staff", 2, 27, 0, {"Str": 1, "Spi": 9}, {("SpellPower", 20)}, ((36, 54), 3300)),
    2033: ("Ambassador's Boots", 2, 28, 62, {"Agi": 3, "Sta": 5, "Spi": 4}, set(), None),
    2906: ("Darkshire Mail Leggings", 2, 28, 168, {"Str": 7, "Spi": 7}, set(), None),
    3400: ("Lucine Longsword", 2, 27, 0, {"Sta": 3, "Spi": 3}, set(), ((24, 46), 2500)),
    3562: ("Belt of Vindication", 3, 29, 57, {"Agi": 7, "Sta": 3}, set(), None),
}
# The Forever effects with no 1.12 equip spell (or none of the Forever value): item: a word of its loot list detail.
LEFT_OUT = {
    2280: "movement impairing", 2942: "25 damage", 71503: "damage done by magical spells", 71505: "Humanoids", 71507: "Block",
    71508: "Undead", 71509: "70 Physical damage", 71510: "Fear", 71511: "Lockpicking", 71513: "resisted",
}


def equip_bonuses(spells, row):
    """The on-equip bonuses of a generated new item row."""
    return {bonus_of(spells, int(row["spellid_%d" % k])) for k in range(1, 6) if row["spelltrigger_%d" % k] == ON_EQUIP}


def stock_equip_bonuses(spells, text, name, entry):
    """The on-equip bonuses that the SQL puts into the first empty spell slot of a changed stock item."""
    block = text[text.index("-- Changed stock item %s (%d)." % (name, entry)):]
    block = block[:block.index("\n-- ", 1)]
    return {bonus_of(spells, int(s)) for s in re.findall(r"`spellid_1`=IF\(@forever_slot=1, (\d+),", block)}


def emptied_equip_slots(text, name, entry):
    """True when the SQL of a changed stock item empties its on-equip spell slots (the other slots stay)."""
    block = text[text.index("-- Changed stock item %s (%d)." % (name, entry)):]
    block = block[:block.index("\n-- ", 1)]
    return "WHERE `spelltrigger_1`=%s;" % ON_EQUIP in block


def weapon_of(row):
    return (int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])


# --- The spec files.

def test_every_stockade_row_names_its_source_and_date():
    for name in (ITEMS, LOOT):
        rows = spec_rows(name)
        assert rows
        for row in rows:
            assert row["source"].startswith(("https://nether.wowhead.com/", "https://www.wowhead.com/forever/", "client "))
            assert re.fullmatch(r"\d{4}-\d{2}-\d{2}", row["date"])


def test_the_new_items_take_ids_of_the_stockade_block():
    rows = spec_rows(ITEMS)
    assert {int(r["entry"]) for r in rows if r["kind"] != "stock"} == set(NEW_ITEMS)
    assert all(int(r["entry"]) in BLOCK for r in rows if r["kind"] != "stock")


# --- Items.

def test_the_stock_boss_drops_get_their_forever_stats(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    text, rows = sql(out), item_rows(out, "forever_item_stock")
    for entry, (name, quality, level, required, armor, stats, bonuses, weapon) in STOCK_DROPS.items():
        row = rows[entry]
        assert (row["name"], int(row["quality"]), int(row["item_level"]), int(row["required_level"]), int(row["armor"]),
                stats_of(row), stock_equip_bonuses(spells, text, name, entry)) == (
                    name, quality, level, required, armor, stats, bonuses), name
        if weapon:
            assert weapon_of(row) == weapon, name
    # Iron Knuckles keeps its stock chance-on-hit spell 13491 (Pummel): its SQL touches no spell slot.
    assert not emptied_equip_slots(text, "Iron Knuckles", 2942)


def test_a_new_item_copies_a_stock_item_of_its_class_subclass_and_slot_and_takes_its_display(out):
    text, gear = sql(out), stock_gear()
    displays, _ = read_dbc(STOCK / "ItemDisplayInfo.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, kind, quality, _, _, _) in NEW_ITEMS.items():
        source = int(re.search(r"^-- Item %s \(%d\), from item (\d+)\.$" % (re.escape(name), entry), text, re.M).group(1))
        assert gear[source][:3] == kind, name
        assert int(rows[entry]["display_id"]) == gear[source][3] and gear[source][3] in displays, name
        assert (rows[entry]["name"], int(rows[entry]["quality"])) == (name, quality)


def test_the_new_items_have_their_forever_stats_as_stock_equip_spells(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, _, _, stats, bonuses, weapon) in NEW_ITEMS.items():
        row = rows[entry]
        assert (stats_of(row), equip_bonuses(spells, row)) == (stats, bonuses), name
        if weapon:
            assert weapon_of(row) == weapon, name


def test_bridgebreaker_bindings_keep_their_forever_fire_resistance(out):
    assert item_rows(out, "forever_item_new")[71504]["fire_res"] == "7"


# --- Boss drops.

def test_each_new_drop_is_a_row_outside_the_stock_groups_on_a_copy_of_the_boss_loot(out):
    text = sql(out)
    mine = {key: row for key, row in loot_rows(text).items() if key[0] in BOSSES}
    # Group 0 (no equal-chance group), one item, patch 0 to 10.
    assert mine == {key: (chance, 0, 1, 1, 0, 10) for key, chance in DROPS.items()}
    assert BOSSES - {BRUEGAL} <= copied_entries(text)
    for entry, item in DROPS:
        copy = text.index("INSERT INTO `%s` SELECT * FROM `creature_loot_template` WHERE `entry`=%d;" % (CREATURE_LOOT, entry))
        assert text.index("VALUES (%d, %d, " % (entry, item)) > copy


def test_bruegal_and_the_stock_references_stay_stock(out):
    # Bruegal Ironknuckle has no new drop, and the stock drops keep their stock chance and group: no Forever row
    # replaces his creature loot or a reference of the Stockade bosses.
    text = sql(out)
    assert BRUEGAL not in copied_entries(text)
    assert not STOCK_REFERENCES & copied_entries(text, REFERENCE_LOOT)
    assert not {key for key in loot_rows(text, REFERENCE_LOOT) if key[0] in STOCK_REFERENCES}


# --- Quest rewards.

def test_each_stockade_quest_offers_its_forever_third_choice(out):
    changes = quest_changes(out)
    assert {quest: changes.get(quest) for quest in QUEST_CHANGES} == QUEST_CHANGES


def test_each_quest_copy_fails_on_other_stock_choices(out):
    text = sql(out)
    for quest, (first, second) in STOCK_CHOICES.items():
        assert ("(SELECT COUNT(*) FROM `forever_quest_new` WHERE NOT (`RewChoiceItemId1`=%d AND `RewChoiceItemId2`=%d AND "
                "`RewChoiceItemId3`=0 AND `RewChoiceItemId4`=0 AND `RewChoiceItemId5`=0 AND `RewChoiceItemId6`=0))=0"
                % (first, second)) in text, quest


def test_the_reward_items_of_the_stockade_quests_get_their_forever_stats(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    text, rows = sql(out), item_rows(out, "forever_item_stock")
    for entry, (name, quality, level, armor, stats, bonuses, weapon) in QUEST_ITEMS.items():
        row = rows[entry]
        assert (row["name"], int(row["quality"]), int(row["item_level"]), int(row["armor"]), stats_of(row),
                stock_equip_bonuses(spells, text, name, entry)) == (name, quality, level, armor, stats, bonuses), name
        if weapon:
            assert weapon_of(row) == weapon, name


# --- The lists.

def test_the_loot_list_names_each_left_out_forever_effect_of_the_stockade(out):
    listed = loot_list(out)
    mine = {int(key[2]): detail for key, detail in listed.items()
            if key[0] == "left-out" and (int(key[2]) in BLOCK or int(key[2]) in STOCK_DROPS or int(key[2]) in QUEST_ITEMS)}
    assert set(mine) == set(LEFT_OUT)
    for item, word in LEFT_OUT.items():
        assert word in mine[item], item


def test_the_stockade_has_no_unconfirmed_or_unreadable_row(out):
    listed = loot_list(out)
    assert not {key for key in listed if key[0] != "left-out" and int(key[2]) in BLOCK}


def test_an_unconfirmed_stockade_drop_is_left_out_and_listed(tmp_path):
    out, done = changed(tmp_path, (LOOT, "1.36\t-\t-\tconfirmed", "1.36\t-\t-\tunconfirmed"))
    assert done.returncode == 0, done.stderr
    assert ("unconfirmed", "drop", "71510") in loot_list(out)
    assert (DEXTREN, 71510) not in loot_rows(sql(out)) and (DEXTREN, 71509) in loot_rows(sql(out))


def test_a_stockade_item_outside_its_block_is_refused(tmp_path):
    error = refused(tmp_path, (ITEMS, "new\t71514\t273829", "new\t71614\t273829"),
                    (LOOT, "drop\t71514\t", "drop\t71614\t"))
    assert "outside the item ID block 71500-71599" in error


def test_a_stockade_boss_under_another_name_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "\t1717\tHamhock\t", "\t1717\tHam Hock\t"))
    assert "is 'Hamhock', not 'Ham Hock'" in error
