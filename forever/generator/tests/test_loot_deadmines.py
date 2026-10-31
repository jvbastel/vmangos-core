"""The Forever loot of the Deadmines (jvbastel/WOWAI#244, spec #240).

The spec rows are in forever/spec/loot_deadmines.tsv (drops, quest rewards) and loot_deadmines_items.tsv (items).
The test runs the generator on the spec folder and reads the generated SQL and the loot list from the outside. The
expected values come from Wowhead's Forever tooltips and quest pages (read 2026-10-31, notes/forever/wowhead-cache,
notes/forever/loot-data-deadmines.md) and the stock world database (boss loot entries, quest rewards).
"""
import re

import pytest

from test_blacksmithing import spec_rows
from test_loot_rfc import (CREATURE_LOOT, REFERENCE_LOOT, changed, copied_entries, item_rows, loot_list, loot_rows,
                           quest_changes, refused, sql, stats_of, stock_gear)
from test_pipeline import STOCK, bonus_of, generate, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

ITEMS, LOOT = "loot_deadmines_items.tsv", "loot_deadmines.tsv"
# The bosses of the Deadmines (creature entry = creature loot entry, stock creature_template.loot_id).
RHAHKZOR, MINER_JOHNSON, SHREDDER, SNEED, GILNID = 644, 3586, 642, 643, 1763
SMITE, GREENSKIN, VANCLEEF, COOKIE = 646, 647, 639, 645
# The stock boss references (one equal-chance group each, groupid 1): they stay stock.
BOSS_REFERENCES = {30065, 30066, 30067, 30068, 30069, 30070, 30071, 30102, 30120}
ON_EQUIP = "1"

# The changed stock boss drops with their Forever values (Wowhead): name, quality, item level, required level,
# armor, base stats, resistances as {column: value}, on-equip bonuses as (bonus, value), (damage, delay) of a
# weapon or None.
STOCK_DROPS = {
    872: ("Rockslicer", 3, 23, 18, 0, {"Str": 7}, {"nature_res": 7}, set(), ((52, 78), 3300)),
    5187: ("Rhahk'Zor's Hammer", 3, 21, 16, 0, {"Sta": 8}, {}, {("SpellPower", 8)}, ((47, 71), 3200)),
    5443: ("Gold-plated Buckler", 3, 22, 17, 509, {"Agi": 2, "Sta": 5}, {}, set(), None),
    5444: ("Miner's Cape", 3, 22, 17, 20, {"Sta": 5}, {}, {("AP", 4), ("Mining", 2)}, None),
    1937: ("Buzz Saw", 3, 23, 18, 0, {"Agi": 3, "Sta": 3}, {}, set(), ((24, 46), 2300)),
    2169: ("Buzzer Blade", 3, 21, 16, 0, {}, {}, {("SpellPower", 22)}, ((9, 18), 1500)),
    5194: ("Taskmaster Axe", 3, 23, 18, 0, {"Sta": 8, "Spi": 8}, {}, {("SpellPower", 15), ("Healing", 30)},
           ((31, 48), 2700)),
    5195: ("Gold-flecked Gloves", 3, 22, 17, 25, {"Str": 7}, {"fire_res": 5}, set(), None),
    5199: ("Smelting Pants", 3, 21, 16, 76, {"Sta": 7, "Spi": 5}, {}, set(), None),
    5192: ("Thief's Blade", 3, 22, 17, 0, {"Agi": 4}, {}, set(), ((17, 32), 1700)),
    5196: ("Smite's Reaver", 3, 21, 17, 0, {"Str": 3, "Spi": 3}, {}, set(), ((18, 35), 1900)),
    5200: ("Impaling Harpoon", 3, 22, 17, 0, {"Agi": 9, "Int": 5}, {}, set(), ((36, 55), 2400)),
    5201: ("Emberstone Staff", 3, 22, 17, 0, {"Sta": 8}, {"fire_res": 8}, {("SpellPower", 24)}, ((34, 51), 3000)),
    10403: ("Blackened Defias Belt", 3, 22, 17, 50, {}, {"arcane_res": 5}, {("AP", 18)}, None),
    5193: ("Cape of the Brotherhood", 3, 24, 19, 21, {"Agi": 5, "Sta": 3}, {}, set(), None),
    5202: ("Corsair's Overshirt", 3, 22, 17, 40, {"Sta": 5, "Spi": 8}, {}, {("SpellPower", 4), ("Healing", 9)}, None),
    10399: ("Blackened Defias Armor", 3, 22, 17, 89, {"Str": 4, "Agi": 3, "Sta": 10}, {"arcane_res": 5}, set(), None),
    5197: ("Cookie's Tenderizer", 3, 21, 16, 0, {"Spi": 4}, {}, set(), ((27, 51), 2800)),
    # Its damage (20-38, speed 1.30) equals the stock row: the SQL keeps it.
    5198: ("Cookie's Stirring Rod", 3, 22, 17, 0, {}, {}, set(), None),
}
# Stock boss drops whose Forever tooltip equals the stock item: no items row.
UNCHANGED_DROPS = {1156, 7230, 5191}
# The stock 1.12 equip spell of +2 Mining (Spell.dbc 13611: apply aura mod skill 186, value 2).
MINING_2 = 13611

# The new items (Wowhead Forever tooltips): entry: (name, Forever item, (class, subclass, slot) of Item.db2, quality,
# armor, base stats, on-equip bonuses as (bonus, value), (damage, delay) of a weapon or None).
NEW_ITEMS = {
    71200: ("Ogre Loincloth", 273289, (4, 1, 7), 3, 35, {"Str": 4, "Sta": 6}, {("SpellPower", 6), ("Healing", 13)}, None),
    71201: ("Bandsaw Wristbands", 273293, (4, 3, 9), 3, 82, {"Sta": 5}, set(), None),
    71202: ("Goblin Hammer", 273297, (2, 4, 21), 3, 20, {"Sta": 4}, set(), ((21, 40), 2000)),
    71203: ("Lookie's Spyglass", 273298, (4, 0, 12), 3, 0, {"Spi": 1}, set(), None),
    71204: ("First Mate Band", 284715, (4, 0, 11), 3, 0, {"Str": 4, "Sta": 5}, set(), None),
    71205: ("Dull Sawblade", 285292, (2, 16, 25), 3, 0, {"Sta": 2}, {("AP", 6)}, ((15, 28), 1700)),
    71206: ("Monastic Hammer", 270005, (2, 4, 21), 2, 0, {"Int": 2}, {("SpellPower", 12)}, ((9, 18), 2100)),
    71207: ("Worn Miner's Waistcord", 270007, (4, 1, 6), 2, 18, {"Sta": 2, "Int": 3}, set(), None),
    71208: ("Miner's Workgloves", 270012, (4, 1, 10), 2, 21, {"Sta": 5}, set(), None),
    71209: ("Miner's Workboots", 270013, (4, 2, 8), 2, 53, {"Spi": 5}, set(), None),
    71210: ("Bravo's Armbands", 270015, (4, 2, 9), 2, 35, {"Str": 3, "Agi": 3}, set(), None),
    71211: ("Dreamer's Leggings", 270016, (4, 2, 7), 2, 71, {"Int": 6}, {("SpellPower", 2)}, None),
}
# The new boss drops (Wowhead Forever "Dropped by" and "Drop Chance"): (creature loot entry, item): chance.
DROPS = {(RHAHKZOR, 71200): 1.56, (SNEED, 71201): 0.95, (GILNID, 71202): 1.30, (COOKIE, 71203): 1.04,
         (SMITE, 71204): 1.01, (SHREDDER, 71205): 1.82}

# The quest reward changes (Wowhead Forever quest pages against the stock quest_template rows).
OH_BROTHER, MEMORIES, BANDANAS, ASSAULT = 167, 168, 214, 2040
MINERS_REVENGE = 1893
QUEST_CHANGES = {
    # Stock: a fixed reward 1893 and no choice. Forever: three choices, 1893 among them, and no fixed reward.
    OH_BROTHER: {"RewChoiceItemId1": "1893", "RewChoiceItemCount1": "1", "RewChoiceItemId2": "71208",
                 "RewChoiceItemCount2": "1", "RewChoiceItemId3": "71209", "RewChoiceItemCount3": "1"},
    MEMORIES: {"RewChoiceItemId3": "71207", "RewChoiceItemCount3": "1"},
    BANDANAS: {"RewChoiceItemId4": "71206", "RewChoiceItemCount4": "1"},
    ASSAULT: {"RewChoiceItemId3": "71210", "RewChoiceItemCount3": "1", "RewChoiceItemId4": "71211",
              "RewChoiceItemCount4": "1"},
}
# The stock quest rewards with Forever stats: quality, item level, base stats, (damage, delay) or None.
QUEST_ITEMS = {
    2042: (3, 24, {"Int": 5, "Spi": 6}, ((36, 55), 3000)),
    2074: (2, 19, {"Str": 1, "Sta": 1}, ((16, 31), 2500)),
    2089: (2, 19, {"Sta": 2}, ((12, 23), 1800)),
    6094: (2, 19, {"Agi": 6}, ((33, 50), 3300)),
    7606: (2, 22, {"Str": 4, "Spi": 2}, None),
    7607: (2, 22, {"Spi": 2}, None),
}
# The rows with a Forever stat or effect that 1.12 has no stock spell for.
LEFT_OUT = {872, 5443, 5444, 2169, 5199, 5192, 5197, 5198, 2042, 71201, 71203}


def equip_bonuses(spells, row):
    """{(bonus, value)} of the on-equip spells of a generated item row; ("Mining", 2) for MINING_2."""
    ids = [int(row["spellid_%d" % k]) for k in range(1, 6) if row["spelltrigger_%d" % k] == ON_EQUIP]
    return {("Mining", 2) if s == MINING_2 else bonus_of(spells, s) for s in ids}


def stock_equip_bonuses(spells, text, entry):
    """{(bonus, value)} of the on-equip spells that the SQL of a changed stock item puts on it. The SQL empties each
    on-equip slot of the stock row first; the stock rows of these items have no other spell (stock world DB), so
    the item ends with exactly these spells."""
    block = re.search(r"^-- Changed stock item .* \(%d\)\.\n((?:.+\n)*)" % entry, text, re.M).group(1)
    ids = [int(s) for s in re.findall(r"`spellid_1`=IF\(@forever_slot=1, (\d+), ", block)]
    if ids:
        assert "SET `spellid_1`=0, `spelltrigger_1`=0" in block
    return {("Mining", 2) if s == MINING_2 else bonus_of(spells, s) for s in ids}


def weapon_of(row):
    return (int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])


# --- The spec files.

def test_every_deadmines_row_names_its_source_and_date():
    for name in (ITEMS, LOOT):
        rows = spec_rows(name)
        assert rows
        for row in rows:
            assert row["source"].startswith(("https://nether.wowhead.com/", "https://www.wowhead.com/forever/", "client "))
            assert re.fullmatch(r"\d{4}-\d{2}-\d{2}", row["date"])


def test_the_new_items_take_their_ids_from_the_deadmines_block():
    rows = spec_rows(ITEMS)
    assert {int(r["entry"]) for r in rows if r["kind"] != "stock"} == set(NEW_ITEMS)
    assert all(71200 <= e <= 71299 for e in NEW_ITEMS)
    assert not UNCHANGED_DROPS & {int(r["entry"]) for r in rows}


# --- Items.

def test_the_stock_boss_drops_get_their_forever_stats_as_stock_equip_spells(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    text, rows = sql(out), item_rows(out, "forever_item_stock")
    for entry, (name, quality, level, required, armor, stats, res, bonuses, weapon) in STOCK_DROPS.items():
        row = rows[entry]
        assert (row["name"], int(row["quality"]), int(row["item_level"]), int(row["required_level"]),
                int(row["armor"]), stats_of(row)) == (name, quality, level, required, armor, stats), name
        resistances = {k: int(v) for k, v in row.items() if k.endswith("_res") and v != "0"}
        assert resistances == res, name
        assert stock_equip_bonuses(spells, text, entry) == bonuses, name
        if weapon:
            assert weapon_of(row) == weapon, name


def test_a_new_item_copies_a_stock_item_of_its_class_subclass_and_slot_and_takes_its_display(out):
    text, gear = sql(out), stock_gear()
    displays, _ = read_dbc(STOCK / "ItemDisplayInfo.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, kind, quality, *_rest) in NEW_ITEMS.items():
        source = int(re.search(r"^-- Item %s \(%d\), from item (\d+)\.$" % (re.escape(name), entry), text, re.M).group(1))
        assert gear[source][:3] == kind, name
        assert int(rows[entry]["display_id"]) == gear[source][3] and gear[source][3] in displays
        assert (rows[entry]["name"], int(rows[entry]["quality"])) == (name, quality)


def test_the_new_items_have_their_forever_stats_as_stock_equip_spells(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, _, _, armor, stats, bonuses, weapon) in NEW_ITEMS.items():
        row = rows[entry]
        assert (int(row["armor"]), stats_of(row)) == (armor, stats), name
        assert equip_bonuses(spells, row) == bonuses, name
        if weapon:
            assert weapon_of(row) == weapon, name


def test_a_new_or_now_blue_item_disenchants_as_a_stock_blue_item_of_its_level(out):
    # Stock blue weapons and armor of item level 20-25 disenchant by disenchant_loot_template 41 (stock world DB:
    # 1156, 5191, 5193, 7230 of these bosses).
    # Cookie's Stirring Rod is blue in stock too (disenchant 41) and not in the stock gear file (flags 256 on its
    # patch 0 row): the copy of its stock row keeps 41. Rhahk'Zor's Hammer and Buzzer Blade were white (disenchant 0).
    rows = {**item_rows(out, "forever_item_new"), **item_rows(out, "forever_item_stock")}
    blue = [e for e, item in NEW_ITEMS.items() if item[3] == 3] + list(STOCK_DROPS)
    kept = {5198: "41"}
    assert {e: int(rows[e].get("disenchant_id", kept.get(e, "0"))) for e in blue} == dict.fromkeys(blue, 41)


# --- Boss drops.

def test_each_new_drop_is_a_row_outside_the_stock_groups_on_a_copy_of_the_boss_loot(out):
    text = sql(out)
    deadmines = {key: row for key, row in loot_rows(text).items() if key[0] in {b for b, _ in DROPS}}
    assert deadmines == {key: (chance, 0, 1, 1, 0, 10) for key, chance in DROPS.items()}
    assert {b for b, _ in DROPS} <= copied_entries(text)
    for entry, item in DROPS:
        copy = text.index("INSERT INTO `%s` SELECT * FROM `creature_loot_template` WHERE `entry`=%d;" % (CREATURE_LOOT, entry))
        assert text.index("VALUES (%d, %d, " % (entry, item)) > copy


def test_the_stock_equal_chance_groups_of_the_bosses_stay_stock(out):
    # The stock drops keep their stock chance: no Forever row replaces a boss reference, and Miner Johnson, Captain
    # Greenskin and Edwin VanCleef (no new drop) keep their stock creature loot.
    text = sql(out)
    assert not {MINER_JOHNSON, GREENSKIN, VANCLEEF} & copied_entries(text)
    assert not BOSS_REFERENCES & copied_entries(text, REFERENCE_LOOT)
    assert not {k for k in loot_rows(text, REFERENCE_LOOT) if k[0] in BOSS_REFERENCES}


def test_the_sql_fails_when_a_boss_does_not_use_its_loot_entry(out):
    guard = ("SELECT IF((SELECT COUNT(*) FROM `creature_template` WHERE `entry`=%d)>0 AND (SELECT COUNT(*) FROM "
             "`creature_template` WHERE `entry`=%d AND `loot_id`<>%d)=0, 0, (SELECT 1 UNION SELECT 2)) INTO @forever_boss;")
    for boss, _ in DROPS:
        assert guard % (boss, boss, boss) in sql(out)


# --- Quest rewards.

def test_the_deadmines_quests_offer_their_forever_reward_choices(out):
    changes = quest_changes(out)
    assert {q: changes[q] for q in QUEST_CHANGES} == QUEST_CHANGES
    assert not {166, 373} & set(changes)


def test_oh_brother_loses_its_fixed_reward_and_fails_on_a_stock_row_without_it(out):
    text = sql(out)
    block = text[text.index("-- Quest %d " % OH_BROTHER):]
    block = block[:block.index("DROP TEMPORARY TABLE")]
    assert ("SELECT IF((SELECT COUNT(*) FROM `forever_quest_new` WHERE NOT (`RewItemId1`=1893 OR `RewItemId2`=1893 OR "
            "`RewItemId3`=1893 OR `RewItemId4`=1893))=0, 0, (SELECT 1 UNION SELECT 2)) INTO @forever_fixed;") in block
    clear = "UPDATE `forever_quest_new` SET %s;" % ", ".join(
        "`RewItemCount{k}`=IF(`RewItemId{k}`={i}, 0, `RewItemCount{k}`), "
        "`RewItemId{k}`=IF(`RewItemId{k}`={i}, 0, `RewItemId{k}`)".format(k=k, i=MINERS_REVENGE) for k in range(1, 5))
    assert clear in block
    assert block.index(clear) < block.index("INSERT INTO `forever_quest_template`")
    # The other Deadmines quests clear no fixed reward.
    for quest in set(QUEST_CHANGES) - {OH_BROTHER}:
        other = text[text.index("-- Quest %d " % quest):]
        assert "RewItemId1`=IF(" not in other[:other.index("DROP TEMPORARY TABLE")]


def test_a_fixed_reward_row_needs_a_free_choice_slot_and_must_not_be_a_choice(tmp_path):
    error = refused(tmp_path, (LOOT, "reward\t1893\tMiner's Revenge\t167", "reward\t2037\tTunneler's Boots\t168"),
                    (LOOT, "Oh Brother. . .\t-\t-\tfixed", "Collecting Memories\t-\t-\tfixed"))
    assert "item 2037 is a reward choice of quest 168 already" in error


def test_the_quest_reward_items_get_their_forever_stats(out):
    rows = item_rows(out, "forever_item_stock")
    for entry, (quality, level, stats, weapon) in QUEST_ITEMS.items():
        row = rows[entry]
        assert (int(row["quality"]), int(row["item_level"]), stats_of(row)) == (quality, level, stats), entry
        if weapon:
            assert weapon_of(row) == weapon, entry
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    assert stock_equip_bonuses(spells, sql(out), 2042) == {("SpellPower", 16), ("Healing", 33)}


# --- The lists.

def test_the_loot_list_names_each_left_out_forever_effect_of_the_deadmines(out):
    listed = loot_list(out)
    ours = set(STOCK_DROPS) | set(NEW_ITEMS) | set(QUEST_ITEMS)
    assert {int(k[2]) for k in listed if k[0] == "left-out" and int(k[2]) in ours} == LEFT_OUT
    assert "Attack Power against Elementals" in listed[("left-out", "item", "5444")]
    assert not {k for k in listed if k[0] != "left-out" and int(k[2]) in ours}


def test_an_unconfirmed_drop_and_an_unreadable_item_are_left_out_and_listed(tmp_path):
    out, done = changed(tmp_path, (LOOT, "1.56\t-\t-\tconfirmed", "1.56\t-\t-\tunconfirmed"),
                        (ITEMS, "\tconfirmed\thttps://nether.wowhead.com/tooltip/item/284715",
                         "\tunreadable\thttps://nether.wowhead.com/tooltip/item/284715"))
    assert done.returncode == 0, done.stderr
    listed = loot_list(out)
    assert ("unconfirmed", "drop", "71200") in listed
    assert ("unreadable", "item", "71204") in listed and ("unreadable", "drop", "71204") in listed
    rows = loot_rows(sql(out))
    assert (RHAHKZOR, 71200) not in rows and (SMITE, 71204) not in rows and (SNEED, 71201) in rows
    assert 71204 not in item_rows(out, "forever_item_new")
