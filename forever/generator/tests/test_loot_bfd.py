"""The Forever loot of Blackfathom Deeps (jvbastel/WOWAI#246, spec #240).

The spec rows are in forever/spec/loot_blackfathom_deeps.tsv (drops, quest rewards) and
loot_blackfathom_deeps_items.tsv (items). The test runs the generator on the spec folder and reads the generated SQL
and the loot list from the outside. The expected values come from Wowhead's Forever tooltips and quest pages (read
2026-10-31, notes/forever/wowhead-cache) and the stock world database (notes/forever/loot-data-blackfathom_deeps.md:
the BFD boss loot entries, references and quest choices). The shared row cases, refusals and core reads are tested
in test_loot_rfc.py.
"""
import re

import pytest

from loot_helpers import (CREATURE_LOOT, REFERENCE_LOOT, bonus_of, changed, copied_entries, item_rows, loot_list,
                          loot_rows, new_equip_spells, ours, quest_changes, spec_drops, sql, stats_of, stock_equip_spells,
                          stock_gear)
from test_pipeline import STOCK, read_dbc

pytestmark = pytest.mark.skipif(not (STOCK / "Spell.dbc").is_file(), reason="no stock DBC folder at %s" % STOCK)

ITEMS, LOOT = "loot_blackfathom_deeps_items.tsv", "loot_blackfathom_deeps.tsv"
GHAMOORA, SAREVESS, GELIHAST, LORGUS, KELRIS, SERRAKIS, AKUMAI = 4887, 4831, 6243, 12902, 4832, 4830, 4829
AQUANIS = 12876  # Baron Aquanis: no new drop, no stock drop row
# The boss references of the stock unique drops (one equal-chance group each, stock world DB).
BOSS_REFERENCES = (30156, 30152, 30178, 30153, 30151, 30150)

# The stock boss drops with their Forever values (Wowhead): quality, armor, base stats, resistances (only the ones
# that are not 0), (damage, delay) of a weapon, on-equip bonuses as (bonus, value).
STOCK_DROPS = {
    6907: ("Tortoise Armor", 3, 248, {}, {}, None, {("SpellDamageReduction", 10)}),
    6908: ("Ghamoo-ra's Bind", 3, 25, {"Sta": 7}, {}, None, {("SpellPower", 8)}),
    888: ("Naga Battle Gloves", 3, 61, {"Str": 4, "Sta": 4}, {}, None, {("SpellPower", 5), ("Healing", 11)}),
    3078: ("Naga Heartpiercer", 3, 0, {}, {}, ((22, 42), 2400), set()),
    11121: ("Darkwater Talwar", 3, 0, {}, {}, ((26, 49), 2200), set()),
    6905: ("Reef Axe", 3, 0, {"Str": 8}, {}, ((59, 89), 3300), {("SpellPower", 8), ("Healing", 15)}),
    6906: ("Algae Fists", 3, 132, {"Str": 8, "Sta": 5}, {"nature_res": 5}, None, set()),
    1155: ("Rod of the Sleepwalker", 3, 0, {"Spi": 11}, {}, ((39, 60), 2800), {("SpellPower", 32)}),
    6903: ("Gaze Dreamer Pants", 3, 40, {"Spi": 9}, {}, None, {("SpellPower", 12)}),
    6901: ("Glowing Thresher Cape", 3, 23, {"Spi": 8}, {}, None, {("SpellPower", 4), ("Healing", 4)}),
    6902: ("Bands of Serra'kis", 3, 43, {"Str": 6, "Sta": 3}, {}, None, set()),
    6904: ("Bite of Serra'kis", 3, 0, {}, {"nature_res": 2}, ((16, 30), 1300), set()),
    6909: ("Strike of the Hydra", 3, 0, {}, {}, ((67, 102), 3300), set()),
    6910: ("Leech Pants", 3, 41, {"Sta": 5}, {}, None, {("SpellPower", 5), ("Mp5", 5), ("HealthRegen", 5)}),
    6911: ("Moss Cinch", 3, 58, {"Sta": 5}, {}, None, set()),
}
# The stock reward items of the BFD quests with their Forever values, as STOCK_DROPS.
STOCK_REWARDS = {
    6743: ("Sustaining Ring", 2, 0, {"Sta": 4}, {}, None, {("HealthRegen", 4)}),
    6998: ("Nimbus Boots", 2, 28, {"Agi": 5}, {}, None, {("SpellPower", 4), ("Healing", 7)}),
    7000: ("Heartwood Girdle", 2, 50, {"Sta": 5}, {}, None, {("SpellPower", 6)}),
    7001: ("Gravestone Scepter", 3, 0, {"Spi": 3}, {}, ((30, 57), 1500), {("ShadowDamage", 6)}),
    7002: ("Arctic Buckler", 3, 642, {"Sta": 8, "Spi": 3}, {"frost_res": 5}, None, set()),
    7003: ("Beetle Clasps", 2, 81, {"Sta": 3}, {}, None, {("AP", 6)}),
    7004: ("Prelacy Cape", 2, 20, {"Spi": 3}, {}, None, {("SpellPower", 2), ("Healing", 4)}),
    16886: ("Outlaw Sabre", 3, 0, {}, {}, ((38, 72), 2700), {("AP", 16)}),
    16887: ("Witch's Finger", 3, 0, {"Sta": 4, "Int": 8}, {}, None, set()),
    17694: ("Band of the Fist", 2, 0, {"Str": 4, "Agi": 4}, {}, None, set()),
    17695: ("Chestnut Mantle", 2, 31, {"Sta": 3}, {}, None, {("SpellPower", 5)}),
}

# The new items (Wowhead Forever tooltips): entry: (name, Forever item, (class, subclass, slot) of Item.db2, quality,
# base stats, on-equip bonuses as (bonus, value), (damage, delay) of a weapon).
NEW_ITEMS = {
    71400: ("Spiked Shell Band", 273839, (4, 0, 11), 3, {"Sta": 5}, {("NatureThorns", 4)}, None),
    71401: ("Cursed Murloc Eye", 273840, (4, 0, 12), 3, {}, set(), None),
    71402: ("Twilight Maul", 273841, (2, 5, 17), 3, {}, {("SpellPower", 12)}, ((60, 91), 3300)),
    71403: ("Treacherous Treads", 273842, (4, 3, 8), 3, {"Str": 5, "Sta": 7}, set(), None),
    71404: ("Fallenroot Longbow", 273843, (2, 2, 15), 3, {"Agi": 2, "Sta": 4}, set(), ((21, 40), 2200)),
    71405: ("Twilight Lord Girdle", 273846, (4, 3, 6), 3, {"Str": 8}, {("BlockValue", 10)}, None),
    71406: ("Staghide Armguards", 270021, (4, 2, 9), 2, {"Agi": 3, "Sta": 3}, set(), None),
    71408: ("Dark Ritual Leggings", 270031, (4, 2, 7), 3, {"Sta": 9}, {("SpellPower", 14)}, None),
    71409: ("Cultist's Armguards", 270032, (4, 2, 9), 3, {"Sta": 7}, {("AP", 10)}, None),
    71410: ("Dreamer's Chestguard", 270043, (4, 2, 5), 3, {"Str": 10, "Sta": 5, "Spi": 11}, set(), None),
}
# The new boss drops of the spec (Wowhead Forever "Dropped by" and "Drop Chance"): (creature loot entry, item): chance.
# The loot entry of each BFD boss is its creature entry (stock creature_template.loot_id).
DROPS = spec_drops(LOOT)
# The quests (stock choices from the stock world DB, Forever choices from the Wowhead Forever quest pages):
# {quest: {changed choice slot: item}}.
QUESTS = {1200: {3: 71408, 4: 71409}, 6561: {3: 71408, 4: 71409}, 1275: {3: 71406}, 6922: {3: 71410}}
# Quest 1199 Twilight Falls: stock gives 6998 and 7000 as fixed rewards; the Forever page lists 6998, 7000 and
# Silvered Gauntlets as choices and Silvered Gauntlets once more as a fixed reward. The row is unconfirmed, and so is
# the item: the quest is its only source (#240 lead decision).
TWILIGHT_FALLS, SILVERED_GAUNTLETS = 1199, 71407


BOSSES = {GHAMOORA, SAREVESS, GELIHAST, LORGUS, KELRIS, SERRAKIS, AKUMAI, AQUANIS}


# --- Items.

def check_stock(rows, expected):
    for entry, (name, quality, armor, stats, resistances, weapon, _) in expected.items():
        row = rows[entry]
        assert (row["name"], int(row["quality"]), int(row["armor"]), stats_of(row)) == (name, quality, armor, stats), name
        for column in ("holy_res", "fire_res", "nature_res", "frost_res", "shadow_res", "arcane_res"):
            if column in row:
                assert int(row[column]) == resistances.get(column, 0), (name, column)
        if weapon:
            assert ((int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])) == weapon, name


def test_the_stock_boss_drops_get_their_forever_stats(out):
    check_stock(item_rows(out, "forever_item_stock"), STOCK_DROPS)


def test_the_stock_quest_rewards_get_their_forever_stats(out):
    check_stock(item_rows(out, "forever_item_stock"), STOCK_REWARDS)


def test_a_changed_stock_item_with_bonuses_gets_them_as_stock_equip_spells(out):
    # The on-equip spells of a changed stock item are put into the first empty slot of the copy of the stock row,
    # after its stock on-equip spells are emptied (items.py); its chance-on-hit spells stay.
    text = sql(out)
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    for entry, item in {**STOCK_DROPS, **STOCK_REWARDS}.items():
        assert {bonus_of(spells, s) for s in stock_equip_spells(text, item[0], entry)} == item[6], item[0]


def test_a_new_bfd_item_copies_a_stock_item_of_its_class_subclass_and_slot_and_takes_its_display(out):
    text, gear = sql(out), stock_gear()
    displays, _ = read_dbc(STOCK / "ItemDisplayInfo.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, kind, quality, _, _, _) in NEW_ITEMS.items():
        source = int(re.search(r"^-- Item %s \(%d\), from item (\d+)\.$" % (re.escape(name), entry), text, re.M).group(1))
        assert gear[source][:3] == kind, name
        assert int(rows[entry]["display_id"]) == gear[source][3] and gear[source][3] in displays
        assert (rows[entry]["name"], int(rows[entry]["quality"])) == (name, quality)


def test_the_new_bfd_items_have_their_forever_stats_as_stock_equip_spells(out):
    spells, _ = read_dbc(STOCK / "Spell.dbc")
    rows = item_rows(out, "forever_item_new")
    for entry, (name, _, _, _, stats, bonuses, weapon) in NEW_ITEMS.items():
        row = rows[entry]
        assert stats_of(row) == stats, name
        equip = {bonus_of(spells, spell) for spell in new_equip_spells(row)}
        assert equip == bonuses, name
        if weapon:
            assert ((int(row["dmg_min1"]), int(row["dmg_max1"])), int(row["delay"])) == weapon, name


# --- Boss drops.

def test_each_new_bfd_drop_is_a_row_outside_the_stock_groups_on_a_copy_of_the_boss_loot(out):
    text = sql(out)
    assert ours(loot_rows(text), BOSSES) == {key: (chance, 0, 1, 1, 0, 10) for key, chance in DROPS.items()}
    assert copied_entries(text) & BOSSES == {GHAMOORA, GELIHAST, LORGUS, KELRIS}
    for entry, item in DROPS:
        copy = text.index("INSERT INTO `%s` SELECT * FROM `creature_loot_template` WHERE `entry`=%d;" % (CREATURE_LOOT, entry))
        assert text.index("VALUES (%d, %d, " % (entry, item)) > copy


def test_the_stock_equal_chance_groups_of_the_bfd_bosses_stay_stock(out):
    # The stock unique drops are in a boss reference; no Forever row copies it, so they keep their stock chance.
    text = sql(out)
    assert not set(BOSS_REFERENCES) & copied_entries(text, REFERENCE_LOOT)
    assert not ours(loot_rows(text, REFERENCE_LOOT), set(BOSS_REFERENCES))
    assert not {SAREVESS, SERRAKIS, AKUMAI} & copied_entries(text)


# --- Quest rewards.

def test_the_bfd_quests_offer_their_forever_reward_choices(out):
    changes = quest_changes(out)
    assert {q: changes.get(q) for q in QUESTS} == {
        q: {k: str(v) for slot, item in slots.items() for k, v in
            (("RewChoiceItemId%d" % slot, item), ("RewChoiceItemCount%d" % slot, 1))} for q, slots in QUESTS.items()}
    assert TWILIGHT_FALLS not in changes


# --- The lists.

def test_the_loot_list_names_each_left_out_forever_effect_and_the_unconfirmed_reward_of_bfd(out):
    listed = loot_list(out)
    ids = {str(e) for e in list(STOCK_DROPS) + list(STOCK_REWARDS) + list(NEW_ITEMS)}
    assert {key for key in listed if key[0] == "left-out" and key[2] in ids} == {
        ("left-out", "item", str(e)) for e in (3078, 11121, 1155, 6904, 6909, 6911, 71400, 71401, 71402, 71403)}
    gauntlets = {("unconfirmed", "reward", str(SILVERED_GAUNTLETS)), ("unconfirmed", "item", str(SILVERED_GAUNTLETS))}
    assert gauntlets <= set(listed)
    assert not {key for key in listed if key[0] not in ("left-out", "manual-source") and key[2] in ids} - gauntlets


def test_an_unconfirmed_bfd_drop_is_left_out_and_listed(tmp_path):
    out, done = changed(tmp_path, (LOOT, "9.96\t-\t-\tconfirmed", "9.96\t-\t-\tunconfirmed"))
    assert done.returncode == 0, done.stderr
    assert ("unconfirmed", "drop", "71404") in loot_list(out)
    rows = loot_rows(sql(out))
    assert (LORGUS, 71404) not in rows and (LORGUS, 71403) in rows
