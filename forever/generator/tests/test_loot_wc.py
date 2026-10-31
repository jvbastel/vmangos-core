"""The Forever loot of Wailing Caverns (jvbastel/WOWAI#243, spec #240, testing decision 1).

The spec rows are in forever/spec/loot_wailing_caverns.tsv (drops, quest rewards) and loot_wailing_caverns_items.tsv
(items). The test runs the generator on the spec folder and reads the generated SQL and the loot list from the
outside. The expected values come from Wowhead's Forever tooltips (read 2026-10-31, notes/forever/wowhead-cache), the
Forever quest pages and the stock world database (notes/forever/loot-data-wailing_caverns.md: the WC boss loot
entries; stock item_template).
"""
import re

import pytest

from test_blacksmithing import spec_rows
from test_loot_rfc import (CREATURE_LOOT, ON_EQUIP, REFERENCE_LOOT, changed, copied_entries, item_rows, loot_list,
                           loot_rows, quest_changes, refused, sql, stats_of, stock_gear)
from test_pipeline import STOCK, bonus_of, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

ITEMS, LOOT = "loot_wailing_caverns_items.tsv", "loot_wailing_caverns.tsv"
# The WC bosses; the loot entry of each is its creature entry (stock creature_template.loot_id).
KRESH, MUTANUS, COBRAHN, PYTHAS, ANACONDRA, SERPENTIS, SKUM, VERDAN, FAERIE_DRAGON = (
    3653, 3654, 3669, 3670, 3671, 3673, 3674, 5775, 5912)

# The stock boss drops with their Forever values (Wowhead): name, quality, item level, required level, armor, base
# stats, (damage, delay) of a weapon.
STOCK_DROPS = {
    5404: ("Serpent's Shoulders", 3, 23, 18, 68, {"Agi": 5, "Spi": 7}, None),
    6446: ("Snakeskin Bag", 2, 25, 0, 0, {}, None),
    10412: ("Belt of the Fang", 3, 21, 16, 49, {"Agi": 3, "Sta": 5}, None),
    6460: ("Cobrahn's Grasp", 3, 22, 17, 107, {"Str": 7, "Agi": 3}, None),
    6465: ("Robe of the Moccasin", 3, 22, 17, 40, {"Str": 3, "Spi": 10}, None),
    10410: ("Leggings of the Fang", 3, 22, 17, 78, {"Str": 5, "Agi": 9, "Sta": 4}, None),
    6447: ("Worn Turtle Shell Shield", 3, 21, 16, 490, {}, None),
    13245: ("Kresh's Back", 3, 23, 18, 528, {}, None),
    6472: ("Stinging Viper", 3, 24, 19, 0, {}, ((30, 57), 2800)),
    6473: ("Armor of the Fang", 3, 23, 18, 91, {"Str": 3, "Int": 3, "Spi": 10}, None),
    6448: ("Tail Spike", 3, 21, 16, 0, {"Str": 3, "Agi": 3}, ((17, 33), 1800)),
    6449: ("Glowing Lizardscale Cloak", 3, 23, 18, 20, {"Agi": 6}, None),
    5970: ("Serpent Gloves", 3, 23, 18, 25, {"Agi": 6}, None),
    6459: ("Savage Trodders", 3, 23, 18, 134, {"Sta": 7}, None),
    6469: ("Venomstrike", 3, 24, 19, 0, {}, ((20, 38), 2400)),
    10411: ("Footpads of the Fang", 3, 22, 17, 61, {"Agi": 6, "Sta": 6}, None),
    6629: ("Sporid Cape", 3, 22, 17, 20, {"Sta": 6}, None),
    6630: ("Seedcloud Buckler", 3, 24, 19, 547, {"Int": 5}, None),
    6631: ("Living Root", 3, 23, 18, 0, {"Sta": 2, "Spi": 5}, ((34, 51), 2900)),
    6461: ("Slime-encrusted Pads", 3, 25, 20, 32, {}, None),
    6463: ("Deep Fathom Ring", 3, 24, 19, 0, {"Sta": 3}, None),
    6627: ("Mutant Scale Breastplate", 3, 23, 18, 194, {"Str": 10, "Sta": 4}, None),
    5243: ("Firebelcher", 3, 23, 18, 0, {}, ((27, 52), 1700)),
    6632: ("Feyscale Cloak", 3, 21, 16, 19, {"Sta": 5}, None),
}
# The on-equip spells that the SQL puts on a changed stock item, in order: the stock spell of each Forever bonus
# (equip_stats.py), then the stock health regeneration spells (stock Spell.dbc "Vitality": 21348 gives 6 health per
# 5 sec, 21607 gives 10). Fungal Regrowth (18764), the stock equip spell of Slime-encrusted Pads, is gone.
STOCK_EQUIP = {
    6465: [9394, 7677],                     # SpellPower 4 (Forever 3), Healing 7 (Forever 6)
    6447: [21624],                          # Mp5 2
    13245: [13383],                         # Defense 5 (stock 7517: Defense 4)
    5970: [9397],                           # SpellPower 7
    6459: [21348],                          # 6 health per 5 sec
    6630: [9395],                           # SpellPower 5
    6631: [9344, 25067],                    # SpellPower 15, Healing 30
    6461: [21625, 21607],                   # Mp5 3, 10 health per 5 sec
    6632: [9394],                           # SpellPower 4 (Forever 3)
    6505: [18049],                          # SpellPower 26
    8071: [9393],                           # SpellPower 2
    10919: [9394],                          # SpellPower 4
}
# Stock items whose stock on-equip spell stays (no SQL on their spell slots): Stinging Viper (18197, chance on hit)
# and Venomstrike (29636 Venom Shot).
KEEPS_STOCK_SPELLS = {6472, 6469}

# The new items (Wowhead Forever tooltips): entry: (name, Forever item, (class, subclass, slot) of Item.db2, quality,
# base stats, on-equip bonuses as (bonus, value), (damage, delay) of a weapon).
NEW_ITEMS = {
    71100: ("Cloak of Hermitic Bliss", 273084, (4, 1, 16), 3, {}, set(), None),
    71101: ("Snake Eye Kaleidoscope", 273088, (4, 0, 2), 3, {"Str": 1, "Agi": 1, "Sta": 1, "Int": 1, "Spi": 1}, set(), None),
    # Mp5 3 is the stock spell 21625 (equip_stats.py).
    71102: ("Slither Cord", 273089, (4, 1, 6), 3, {}, {("Mp5", 3)}, None),
    71103: ("Skum's Bucket", 273137, (4, 0, 23), 3, {"Sta": 3}, {("SpellPower", 4), ("Healing", 7)}, None),
    71104: ("Heat Resistant Mitts", 270008, (4, 2, 10), 2, {"Agi": 4, "Sta": 1}, set(), None),
    71105: ("Safety Boots", 270009, (4, 3, 8), 2, {"Str": 4, "Sta": 2}, set(), None),
    71106: ("Hammerbone", 270018, (2, 5, 17), 3, {"Str": 7, "Spi": 10}, set(), ((55, 84), 3400)),
}
# The new boss drops (Wowhead Forever "Dropped by" and "Drop Chance"): (creature loot entry, item): chance.
DROPS = {(KRESH, 71100): 1.17, (ANACONDRA, 71101): 2.04, (PYTHAS, 71102): 1.71, (SKUM, 71103): 2.63}
# Quest 914 Leaders of the Fang: stock choices 6505 and 6504, Forever adds Hammerbone. Quest 962 Serpentbloom: no
# stock choice (fixed reward 10919), Forever shows the choices 10919, Heat Resistant Mitts and Safety Boots.
LEADERS, SERPENTBLOOM = 914, 962
# The resistances (item_template columns) of the items that have one in Forever.
RESISTANCES = {6449: {"nature_res": "2"}, 6631: {"nature_res": "9"},
               71101: {"holy_res": "0", "fire_res": "1", "nature_res": "1", "frost_res": "1", "shadow_res": "1",
                       "arcane_res": "1"}}


@pytest.fixture(scope="module")
def out(tmp_path_factory):
    return generate(tmp_path_factory.mktemp("forever243"))


def stock_spells_put(out, entry):
    """The on-equip spells, in order, that the SQL puts on the changed stock item `entry` (None: no spell SQL)."""
    block = re.search(r"^-- Changed stock item [^\n]* \(%d\)\.$(.*?)^DROP TEMPORARY TABLE" % entry, sql(out), re.M | re.S)
    assert block, entry
    if "SET @forever_slot" not in block.group(1) and "`spellid_1`=0" not in block.group(1):
        return None
    return [int(s) for s in re.findall(r"`spellid_1`=IF\(@forever_slot=1, (\d+), `spellid_1`\)", block.group(1))]


# --- The spec files.

def test_every_wc_loot_row_names_its_source_and_date():
    for name in (ITEMS, LOOT):
        rows = spec_rows(name)
        assert rows
        for row in rows:
            assert row["source"].startswith(("https://nether.wowhead.com/", "https://www.wowhead.com/forever/", "client "))
            assert re.fullmatch(r"\d{4}-\d{2}-\d{2}", row["date"])


def test_the_new_wc_items_come_from_the_wc_item_id_block():
    assert {int(r["entry"]) for r in spec_rows(ITEMS) if r["kind"] != "stock"} == set(NEW_ITEMS)
    assert all(71100 <= entry <= 71199 for entry in NEW_ITEMS)


# --- Items.

def test_the_stock_wc_boss_drops_get_their_forever_stats(out):
    rows = item_rows(out, "forever_item_stock")
    for entry, (name, quality, level, required, armor, stats, weapon) in STOCK_DROPS.items():
        row = rows[entry]
        assert (row["name"], int(row["quality"]), int(row["item_level"]), int(row["required_level"]),
                int(row["armor"]), stats_of(row)) == (name, quality, level, required, armor, stats), name
        assert row["bonding"] == "1", name
        if weapon:
            assert ((int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])) == weapon, name


# The shield block values of Forever (item_template `block`; stock 6447: 6, 13245: 9). The other rows keep their block.
BLOCKS = {6447: "9", 13245: "10", 6630: "11"}


def test_the_wc_shields_get_their_forever_block_value(out):
    rows = item_rows(out, "forever_item_stock")
    assert {entry: row["block"] for entry, row in rows.items() if "block" in row} == BLOCKS


def test_a_block_value_that_is_no_number_is_refused(tmp_path):
    error = refused(tmp_path, (ITEMS, "\tBlock 10\t", "\tBlock ten\t"))
    assert "Kresh's Back" in error and "Block ten" in error


def test_the_forever_bonuses_of_changed_stock_items_are_stock_equip_spells(out):
    for entry, spells in STOCK_EQUIP.items():
        assert stock_spells_put(out, entry) == spells, entry
    for entry in KEEPS_STOCK_SPELLS:
        assert stock_spells_put(out, entry) is None, entry


def test_the_forever_resistances_are_set(out):
    rows = {**item_rows(out, "forever_item_new"), **item_rows(out, "forever_item_stock")}
    for entry, columns in RESISTANCES.items():
        assert {c: rows[entry][c] for c in columns} == columns, entry


def test_a_new_wc_item_copies_a_stock_item_of_its_class_subclass_and_slot_and_takes_its_display(out):
    text, gear = sql(out), stock_gear()
    displays, _ = read_dbc(STOCK / "ItemDisplayInfo.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, kind, quality, _, _, _) in NEW_ITEMS.items():
        source = int(re.search(r"^-- Item %s \(%d\), from item (\d+)\.$" % (re.escape(name), entry), text, re.M).group(1))
        assert gear[source][:3] == kind, name
        assert int(rows[entry]["display_id"]) == gear[source][3] and gear[source][3] in displays
        assert (rows[entry]["name"], int(rows[entry]["quality"])) == (name, quality)


def test_the_new_wc_items_have_their_forever_stats_as_stock_equip_spells(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, _, _, stats, bonuses, weapon) in NEW_ITEMS.items():
        row = rows[entry]
        assert stats_of(row) == stats, name
        equip = {bonus_of(spells, int(row["spellid_%d" % k])) for k in range(1, 6)
                 if row["spelltrigger_%d" % k] == ON_EQUIP}
        assert equip == bonuses, name
        if weapon:
            assert ((int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])) == weapon


def test_a_new_or_now_blue_wc_item_disenchants_as_a_stock_blue_item_of_its_level(out):
    # Stock blue weapons and armor of item level 20-25 disenchant by disenchant_loot_template 41 (stock world DB).
    rows = {**item_rows(out, "forever_item_new"), **item_rows(out, "forever_item_stock")}
    blue = [e for e, item in NEW_ITEMS.items() if item[3] == 3] + [e for e, item in STOCK_DROPS.items() if item[1] == 3]
    assert {e: int(rows[e]["disenchant_id"]) for e in blue} == dict.fromkeys(blue, 41)


# --- Boss drops.

def test_each_new_wc_drop_is_a_row_outside_the_stock_groups_on_a_copy_of_the_boss_loot(out):
    text = sql(out)
    wc = {key: value for key, value in loot_rows(text).items() if key[0] in
          (KRESH, MUTANUS, COBRAHN, PYTHAS, ANACONDRA, SERPENTIS, SKUM, VERDAN, FAERIE_DRAGON)}
    # Group 0 (no equal-chance group), one item, from patch 0 (Wailing Caverns is open from the first patch) to 10.
    assert wc == {key: (chance, 0, 1, 1, 0, 10) for key, chance in DROPS.items()}
    assert {KRESH, ANACONDRA, PYTHAS, SKUM} <= copied_entries(text)
    for entry, item in DROPS:
        copy = text.index("INSERT INTO `%s` SELECT * FROM `creature_loot_template` WHERE `entry`=%d;" % (CREATURE_LOOT, entry))
        assert text.index("VALUES (%d, %d, " % (entry, item)) > copy


def test_the_bosses_with_no_new_drop_keep_their_stock_loot(out):
    # Their stock drops keep the stock chance and group: no Forever row replaces their creature loot or a reference.
    text = sql(out)
    assert not {COBRAHN, SERPENTIS, VERDAN, MUTANUS, FAERIE_DRAGON} & copied_entries(text)
    assert not {30122, 30123, 30124, 30125, 30126, 30127, 30128, 30162, 30163, 30169} & copied_entries(text, REFERENCE_LOOT)


# --- Quest rewards.

def test_leaders_of_the_fang_offers_hammerbone_and_serpentbloom_the_new_gloves_and_boots(out):
    changes = quest_changes(out)
    assert changes[LEADERS] == {"RewChoiceItemId3": "71106", "RewChoiceItemCount3": "1"}
    assert changes[SERPENTBLOOM] == {"RewChoiceItemId1": "71104", "RewChoiceItemCount1": "1",
                                     "RewChoiceItemId2": "71105", "RewChoiceItemCount2": "1"}


def test_the_reward_items_of_the_wc_quests_get_their_forever_stats(out):
    rows = item_rows(out, "forever_item_stock")
    # Wowhead Forever: (quality, item level, base stats, damage) of the changed stock rewards of 914, 962 and 1487.
    expected = {6505: (3, 24, {"Agi": 7, "Sta": 7, "Int": 7}, ("35", "53")),
                8071: (2, 23, {}, ("21", "39")),
                10919: (2, 20, {}, None)}
    assert {e: (int(rows[e]["quality"]), int(rows[e]["item_level"]), stats_of(rows[e]),
                (rows[e]["dmg_min1"], rows[e]["dmg_max1"]) if "dmg_min1" in rows[e] else None)
            for e in expected} == expected
    assert rows[10919]["armor"] == "21"


# --- The lists.

def test_the_loot_list_names_each_left_out_wc_stat_and_unconfirmed_row(out):
    listed = loot_list(out)
    wc_items = {str(e) for e in list(STOCK_DROPS) + list(NEW_ITEMS) + [6505, 8071, 10919]}
    left_out = {"5243", "6447", "6463", "6469", "6629", "10919", "71100", "71102", "71103"}
    assert {key for key in listed if key[0] == "left-out" and key[2] in wc_items} == {("left-out", "item", e) for e in left_out}
    assert "Movement speed reduced by 25%" in listed[("left-out", "item", "71100")]
    assert "Spell Penetration" in listed[("left-out", "item", "6463")]
    assert "Herbalism" in listed[("left-out", "item", "10919")]
    assert "+40 Armor" in listed[("left-out", "item", "6447")] and "Block" not in listed[("left-out", "item", "6447")]
    # Night Watch Shortsword on Verdan: Method.gg lists it, the tooltip names no source. Apothecary Gloves as a
    # choice of Serpentbloom: the quest page shows it as a choice and as a fixed reward.
    assert ("unconfirmed", "drop", "935") in listed and ("unconfirmed", "reward", "10919") in listed
    assert (VERDAN, 935) not in loot_rows(sql(out))


# --- The case that #242 did not cover: an unconfirmed drop with no chance (its source shows none).

def test_a_confirmed_drop_with_no_chance_is_refused(tmp_path):
    error = refused(tmp_path, (LOOT, "Lady Anacondra\tcreature\t2.04\t", "Lady Anacondra\tcreature\t-\t"))
    assert "Snake Eye Kaleidoscope" in error and "chance" in error


def test_an_unconfirmed_drop_with_a_chance_is_still_read(tmp_path):
    out, done = changed(tmp_path, (LOOT, "Lady Anacondra\tcreature\t2.04\t-\t-\tconfirmed",
                                   "Lady Anacondra\tcreature\t2.04\t-\t-\tunconfirmed"))
    assert done.returncode == 0, done.stderr
    assert ("unconfirmed", "drop", "71101") in loot_list(out)
    assert (ANACONDRA, 71101) not in loot_rows(sql(out))


# --- The case that #242 did not cover: a stock common item that Forever makes blue (no row in the stock gear file).

def test_a_stock_row_class_that_is_not_the_class_of_its_stock_gear_is_refused(tmp_path):
    error = refused(tmp_path, (ITEMS, "Kresh's Back\trare\t23\t18\tpickup\t-\t", "Kresh's Back\trare\t23\t18\tpickup\t2\t"))
    assert "Kresh's Back" in error and "class 2 is not the class 4 of the stock item" in error
